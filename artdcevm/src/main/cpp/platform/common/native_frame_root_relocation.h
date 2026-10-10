#pragma once
#include "platform/common/native_typed_heap_walk.h"
#include "platform/common/decoded_frame_recovery.h"
#include "platform/common/source_commit_plan_identity.h"
#include "platform/common/rooted_schema_plan.h"
#include "platform/common/object_replacement_map.h"
#include "platform/abi/arm64/context.h"
#include <vector>
#include <cstring>
#include <limits>

namespace artdcevm::native_source_commit {class Services;}
namespace artdcevm::native_frame_root_relocation {
// A synchronous root census and relocation check; this alone does NOT grant
// return continuation, deoptimization eligibility or unaffected-code authority.
// Fixed ed6c Thread::ReferenceMapVisitor non-precise mode emits ascending stack
// mask bits followed by ascending GPR mask bits. Stack roots are POINTERS TO A
// TEMPORARY local, whereas GPR roots are the actual original spill address.
struct Frame {
 void* thread=nullptr;uint32_t thread_id=0;void* method=nullptr;
 void** quick=nullptr;const void* header=nullptr;uintptr_t pc=0;
 abi::arm64::context::Prefix* context=nullptr;uint32_t frame_bytes=0;
 code_info_view::SourceFamily family=code_info_view::SourceFamily::kAndroid11;
};
class Proof;
// Only the concrete source-root Proof may issue this interface. This preserves
// its provenance while letting frame walkers call the real implementation
// without embedding observer/link dependencies in every scanner consumer.
class Interface {
 public:
 enum class Failure {None,Preparation,Owner,Plan,Capacity,Frame,Mask,Memory,
                     OriginalCursor,OriginalOrder,OriginalValue,MissingRoot,
                     OriginalStream,ReplacementNotObserved,RelocatedValue};
 struct Result {Failure failure=Failure::Preparation;size_t frames=0,slots=0,callbacks=0;
  native_typed_heap_walk::RootReport stream{};bool completed=false;};
 virtual ~Interface()=default;
 Interface(const Interface&)=delete;Interface&operator=(const Interface&)=delete;
 Interface(Interface&&)=delete;Interface&operator=(Interface&&)=delete;
 virtual bool Begin(const native_source_stop::Session&,void*,const modern_field_metadata::ReadContext&,const source_commit::Plan&,const prepared_roots::Resolver&)noexcept=0;
 virtual bool CaptureFrame(const Frame&,const code_info_view::View&,uint32_t)noexcept=0;
 virtual Result Seal(const native_typed_heap_walk::RootProvenance&)noexcept=0;
 virtual bool BeginRecapture(const source_commit::Plan&,bool)noexcept=0;
 virtual bool EndRecapture()noexcept=0;
 virtual bool VerifyLocationRoot(const Frame&,const void*,void*)const noexcept=0;
 private:
 friend class Proof;Interface()=default;
};
class Proof final:public Interface,private native_typed_heap_walk::RootSink {
 public:
 Proof()=default;Proof(const Proof&)=delete;Proof& operator=(const Proof&)=delete;
 // Ordinary-only allocation, no fixed frame/slot ceiling. A census whose
 // independently prepared capacity is exceeded fails; it never truncates.
 bool PrepareIdentity(const source_commit::Plan&plan,const prepared_roots::Registrar&roots){
  if(identity_prepared_||plan.old_classes.size()!=plan.new_classes.size()||plan.old_instances.size()!=plan.new_instances.size())return false;
  identity_=plan;replacements_.reserve(plan.old_classes.size()+plan.old_instances.size());
  if(!rooted_schema_plan::CollectBindings(&identity_,roots,&identity_roots_))return false;
  plan_=&plan;identity_prepared_=true;return true;
 }
 bool ReserveFramesSlots(size_t frames,size_t slots){
  if(!identity_prepared_||prepared_)return false;
  frames_.reserve(frames);slots_.reserve(slots);scratch_.reserve(slots);prepared_=true;return true;
 }
 bool Prepare(const source_commit::Plan&plan,size_t frames,size_t slots,const prepared_roots::Registrar&roots){
  return PrepareIdentity(plan,roots)&&ReserveFramesSlots(frames,slots);
 }
 bool Begin(const native_source_stop::Session& stop,void* self,const modern_field_metadata::ReadContext& read,const source_commit::Plan& plan,const prepared_roots::Resolver& roots)noexcept override{
  result_={};recapturing_=recapture_complete_=after_=false;sealed_=false;replaced_=false;capturing_=false;frames_.clear();slots_.clear();
  if(!prepared_||plan_!=&plan||!identity_roots_.CanRefresh(roots))return Deny(Failure::Plan);
  identity_roots_.Refresh(roots);
  if(!source_commit::SameIdentity(identity_,plan))return Deny(Failure::Plan);
  if(!self||!read.owner||!read.extent||(!stop.IsFinalWindow(self)&&!stop.IsCollectedStop(self)))return Deny(Failure::Owner);
  stop_=&stop;self_=self;read_=&read;final_=stop.IsFinalWindow(self);capturing_=true;result_.failure=Failure::None;return true;
 }
 // Called synchronously by the original physical-frame walker. No borrowed
 // CodeInfo/table/Context pointer is retained beyond this call.
 bool CaptureFrame(const Frame& f,const code_info_view::View& view,uint32_t row)noexcept override{
  if(!Owner()||!capturing_||(!recapturing_&&sealed_))return Deny(Failure::Owner);
  if(!f.thread||!f.method||!f.quick||!f.header||!f.context||f.frame_bytes<sizeof(uintptr_t)||f.frame_bytes%4)return Deny(Failure::Frame);
  if(!recapturing_&&frames_.size()==frames_.capacity())return Deny(Failure::Capacity);
  for(const auto&table:view.tables){if(!table.region.bit_size)continue;
   if(table.region.bit_start>SIZE_MAX-table.region.bit_size||table.region.bit_start+table.region.bit_size>SIZE_MAX-7||
      !Mapped(table.region.data,(table.region.bit_start+table.region.bit_size+7)/8))return Deny(Failure::Memory);}
  if(!Mapped(f.quick,f.frame_bytes))return Deny(Failure::Memory);
  void* actual=nullptr;std::memcpy(&actual,f.quick,sizeof(actual));if(actual!=f.method)return Deny(Failure::Frame);
  SavedFrame* recapture=nullptr;
  for(auto& old:frames_)if(old.key.thread==f.thread&&old.key.quick==f.quick){if(!recapturing_||old.recaptured)return Deny(Failure::Frame);recapture=&old;}
  if(recapturing_&&(!recapture||!SameFrame(recapture->key,f)))return Deny(Failure::Frame);
  uint32_t stack_index=0,register_index=0,registers=0;
  if(!view.StackMaps().Read(row,4,&stack_index)||!view.StackMaps().Read(row,3,&register_index))return Deny(Failure::Mask);
  if(register_index!=UINT32_MAX){uint32_t value=0,shift=0;
   if(!view.tables[1].Read(register_index,0,&value)||!view.tables[1].Read(register_index,1,&shift)||shift>=32||value>UINT32_MAX>>shift)return Deny(Failure::Mask);
   registers=value<<shift;
  }
  if(recapturing_)scratch_.clear();
  const size_t first=recapturing_?0:slots_.size();
  if(stack_index!=UINT32_MAX){
   const auto& table=view.tables[2];
   for(size_t bit=0;bit<table.row_bits;++bit){bool set=false;
    if(bit>UINT32_MAX||!decoded_frame_recovery::MaskBit(table,stack_index,uint32_t(bit),&set))return Deny(Failure::Mask);
    if(!set)continue;
    if(bit>=f.frame_bytes/4)return Deny(Failure::Mask);
    auto* address=reinterpret_cast<const unsigned char*>(f.quick)+bit*4;
    uint32_t value=0;std::memcpy(&value,address,sizeof(value));
    if(!Add(address,reinterpret_cast<void*>(uintptr_t(value)),false,UINT32_MAX))return false;
   }
  }
  for(uint32_t reg=0;reg<32;++reg)if(registers&(uint32_t{1}<<reg)){
   if(!f.context->IsAccessibleGPR(reg))return Deny(Failure::Frame);
   auto* address=f.context->GetGPRAddress(reg);
   if(!Mapped(address,sizeof(uintptr_t)))return Deny(Failure::Memory);
   uintptr_t value=0;std::memcpy(&value,address,sizeof(value));
   if(f.context->GetGPR(reg)!=value)return Deny(Failure::Frame);
   if(!Add(address,reinterpret_cast<void*>(value),true,reg))return false;
  }
  uintptr_t lr=0;std::memcpy(&lr,reinterpret_cast<const unsigned char*>(f.quick)+f.frame_bytes-sizeof(lr),sizeof(lr));
  if(recapturing_){
   if(recapture->lr!=lr||scratch_.size()!=recapture->limit-recapture->first)return Deny(Failure::Frame);
   for(size_t i=0;i<scratch_.size();++i){const auto&a=slots_[recapture->first+i];const auto&b=scratch_[i];
    if(a.address!=b.address||a.pointer!=b.pointer||a.reg!=b.reg||b.before!=(after_?Replacement(a.before):a.before))return Deny(Failure::RelocatedValue);}
   recapture->recaptured=true;return true;
  }
  SavedFrame saved; saved.lr=lr;saved.key=f;saved.key.context=nullptr;saved.first=first;saved.limit=slots_.size();saved.next=first;
  frames_.push_back(saved);result_.frames=frames_.size();result_.slots=slots_.size();return true;
 }
 Result Seal(const native_typed_heap_walk::RootProvenance& roots)noexcept override{
  if(!Owner()||!capturing_){Deny(Failure::Owner);return result_;}
  after_=false;for(auto&frame:frames_)frame.origin_seen=false;result_.stream=roots.ObserveFrameRoots(*stop_,self_,*read_,*this);
  if(!result_.stream.completed){if(result_.failure==Failure::None)Deny(Failure::OriginalStream);return result_;}
  for(auto&frame:frames_){SkipNull(frame);if(frame.next!=frame.limit||(!frame.origin_seen&&HasGpr(frame))){Deny(Failure::MissingRoot);return result_;}}
  capturing_=false;sealed_=true;result_.completed=true;return result_;
 }
 // Original-call receipts are deliberately private. A caller cannot assert
 // that replacement happened. The concrete native source service is the sole
 // owner allowed to bracket its named ReplaceReferences call.
 bool ValidateBeforeReplacement(const source_commit::Plan& plan)const noexcept{
  return Owner()&&sealed_&&result_.completed&&plan_==&plan&&source_commit::SameIdentity(identity_,plan);
 }
 // The concrete whole-frame owner must rewalk every physical frame, including
 // null GC-mask slots, synchronously in this exact source stop.
 bool BeginRecapture(const source_commit::Plan&plan,bool after)noexcept override{
  if(!Owner()||!sealed_||plan_!=&plan||(after&&!replaced_)||(!after&&replaced_))return Deny(Failure::Owner);
  after_=after;recapturing_=capturing_=true;recapture_complete_=false;
  for(auto&frame:frames_)frame.recaptured=false;return true;
 }
 bool EndRecapture()noexcept override{
  if(!Owner()||!recapturing_)return Deny(Failure::Owner);
  for(const auto&frame:frames_)if(!frame.recaptured)return Deny(Failure::MissingRoot);
  recapturing_=capturing_=false;recapture_complete_=true;return true;
 }
 bool VerifyLocationRoot(const Frame&frame,const void*address,void*value)const noexcept override{
  if(!Owner()||!sealed_||!result_.completed||!address)return false;
  for(const auto&f:frames_)if(SameFrame(f.key,frame))for(size_t i=f.first;i<f.limit;++i){const auto&s=slots_[i];
   void*actual=nullptr;if(s.address==address&&ReadSlot(s,&actual)&&actual==value&&value==(after_?Replacement(s.before):s.before))return true;}
  return false;
 }
 bool VerifyBeforeReplacement(const native_typed_heap_walk::RootProvenance& roots,const source_commit::Plan& plan)noexcept{
  if(!ValidateBeforeReplacement(plan)||replaced_||!recapture_complete_||after_)return Deny(Failure::Owner);
  capturing_=true;for(auto&frame:frames_)frame.next=frame.first;result_.callbacks=0;
  return Seal(roots).completed;
 }
 Result VerifyAfter(const native_typed_heap_walk::RootProvenance& roots,const source_commit::Plan& plan)noexcept{
  result_.completed=false;
  if(!Owner()||!sealed_||!replaced_||plan_!=&plan||!recapture_complete_||!after_)return FailResult(Failure::ReplacementNotObserved);
  after_=true;result_.callbacks=0;for(auto&f:frames_){f.next=f.first;f.origin_seen=false;}
  // Validate physical storage independently before observing the original
  // callback stream. The immutable replacement pairs were copied ordinary.
  for(const auto&s:slots_){void* actual=nullptr;if(!ReadSlot(s,&actual))return FailResult(Failure::Memory);
   if(actual!=Replacement(s.before))return FailResult(Failure::RelocatedValue);}
  result_.stream=roots.ObserveFrameRoots(*stop_,self_,*read_,*this);
  if(!result_.stream.completed){if(result_.failure==Failure::None)Deny(Failure::OriginalStream);return result_;}
  for(auto&f:frames_){SkipNull(f);if(f.next!=f.limit||(!f.origin_seen&&HasGpr(f)))return FailResult(Failure::MissingRoot);}
  result_.completed=true;return result_;
 }
 private:
 friend class native_source_commit::Services;
 bool BindExactReplacement(const source_commit::Plan&plan,const object_replacement::ObjectReplacementMap&map)noexcept{
  if(!ValidateBeforeReplacement(plan)||!final_||map.Size()>replacements_.capacity())return Deny(Failure::Plan);
  replacements_.clear();
  for(size_t i=0;i<map.Size();++i){const auto&node=map.Nodes()[i];
   bool planned=false;for(auto*keys:{&identity_.old_classes,&identity_.old_instances})for(auto*key:*keys)if(key==node.key)planned=true;
   if(!planned||!node.key||!node.value||ReplacementFromPlan(node.key)!=node.value)return Deny(Failure::Plan);
   for(const auto&old:replacements_)if(old.first==node.key)return Deny(Failure::Plan);
   replacements_.push_back({node.key,node.value});
  }
  for(auto*keys:{&identity_.old_classes,&identity_.old_instances})for(auto*key:*keys){bool seen=false;for(const auto&p:replacements_)if(p.first==key)seen=true;if(!seen)return Deny(Failure::Plan);}
  return true;
 }
 bool OriginalReplacementReturned(const source_commit::Plan& plan)noexcept{
  if(!ValidateBeforeReplacement(plan)||!final_||replaced_||replacements_.empty())return Deny(Failure::ReplacementNotObserved);
  replaced_=true;return true;
 }
 struct Slot {const void* address;void* before;bool pointer;uint32_t reg;};
 struct SavedFrame {Frame key{};size_t first=0,limit=0,next=0;uintptr_t lr=0;bool origin_seen=false,recaptured=false;};
 bool identity_prepared_=false,prepared_=false,capturing_=false,sealed_=false,replaced_=false,after_=false,final_=false,recapturing_=false,recapture_complete_=false;
 const source_commit::Plan* plan_=nullptr;source_commit::Plan identity_{};prepared_roots::Bindings identity_roots_;
 const native_source_stop::Session* stop_=nullptr;void*self_=nullptr;
 const modern_field_metadata::ReadContext* read_=nullptr;
 std::vector<SavedFrame>frames_;std::vector<Slot>slots_,scratch_;
 std::vector<std::pair<void*,void*>>replacements_;Result result_{};
 bool Owner()const noexcept{return stop_&&read_&&(final_?stop_->IsFinalWindow(self_):stop_->IsCollectedStop(self_));}
 bool Mapped(const void*p,size_t n)const noexcept{size_t extent=0;return p&&n&&read_&&read_->extent(read_->owner,p,&extent)&&extent>=n;}
 bool Deny(Failure f)noexcept{if(result_.failure==Failure::None||result_.failure==Failure::Preparation)result_.failure=f;result_.completed=false;capturing_=false;return false;}
 Result FailResult(Failure f)noexcept{Deny(f);return result_;}
 bool Add(const void*p,void*v,bool pointer,uint32_t reg)noexcept{
  auto&out=recapturing_?scratch_:slots_;
  if(out.size()==out.capacity())return Deny(Failure::Capacity);
  out.push_back({p,v,pointer,reg});return true;
 }
 bool ReadSlot(const Slot&s,void**value)const noexcept{
  if(!Mapped(s.address,s.pointer?sizeof(uintptr_t):sizeof(uint32_t)))return false;
  if(s.pointer){uintptr_t v;std::memcpy(&v,s.address,sizeof(v));*value=reinterpret_cast<void*>(v);}
  else{uint32_t v;std::memcpy(&v,s.address,sizeof(v));*value=reinterpret_cast<void*>(uintptr_t(v));}return true;
 }
 void* ReplacementFromPlan(void*value)const noexcept{
  for(size_t i=0;i<identity_.old_classes.size();++i)if(identity_.old_classes[i]==value)return identity_.new_classes[i];
  for(size_t i=0;i<identity_.old_instances.size();++i)if(identity_.old_instances[i]==value)return identity_.new_instances[i];
  return value;
 }
 void* Replacement(void*value)const noexcept{for(const auto&pair:replacements_)if(pair.first==value)return pair.second;return value;}
 void SkipNull(SavedFrame&f)noexcept{while(f.next<f.limit&&!slots_[f.next].before)++f.next;}
 static bool SameFrame(const Frame&a,const Frame&b)noexcept{return a.thread==b.thread&&a.method==b.method&&a.quick==b.quick&&a.header==b.header&&a.pc==b.pc&&a.frame_bytes==b.frame_bytes&&a.family==b.family&&(!a.thread_id||!b.thread_id||a.thread_id==b.thread_id);}
 bool HasGpr(const SavedFrame&f)const noexcept{for(size_t i=f.first;i<f.limit;++i)if(slots_[i].pointer)return true;return false;}
 bool Root(const native_typed_heap_walk::RootRecord&r)noexcept override;
};
}
