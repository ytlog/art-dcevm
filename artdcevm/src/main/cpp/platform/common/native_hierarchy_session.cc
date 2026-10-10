#include "platform/common/native_hierarchy_session.h"
#include "platform/common/native_source_class_loading.h"
#include "platform/common/native_hierarchy_private_ledger.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/common/class_redefinition_prefix.h"
#include "platform/abi/arm64/common/redefiner_prefix.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#if defined(__ANDROID__)
#include <android/log.h>
#else
#include <cstdio>
#endif
namespace artdcevm::native_hierarchy_session {
namespace {
thread_local Session* current=nullptr;
thread_local CalibrationScope* calibration=nullptr;
bool Mapped(const void*p,size_t n){size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=n;}
void Error(std::string*e,const char*s){if(e)*e=s;}
using FinishFunction=bool(*)(void*,transaction_root::Holder*,void*);
using AllocateFunction=abi::arm64::android_common::HierarchyAllocator;
using Object=abi::arm64::android11::ReleaseObjectPointer;
FinishFunction finish_original=nullptr;void*finish_token=nullptr;void*finish_target=nullptr;AllocateFunction allocate_original=nullptr;
abi::arm64::android_common::HierarchyLoadClass load_original=nullptr;
bool(*ids_original)(const void*)=nullptr;
native_hierarchy_allocator::Service* installed_service=nullptr;
std::mutex installation;
bool FinishProxy(void*o,transaction_root::Holder*h,void*i){
 auto*s=Session::Current();if(!s||!s->OwnsClassOwner(o)){
  auto*trace=native_source_class_loading::Run::Current();
  abi::arm64::android_common::ClassRedefinitionPrefix def{};
  if(!trace||!Mapped(o,sizeof(def)))return finish_original(o,h,i);
  std::memcpy(&def,o,sizeof(def));
  if(!trace->OwnsDriver(def.driver))return finish_original(o,h,i);
  // Passive ordinary source calibration never changes original success/failure.
  const bool opened=trace->BeforeFinish(o,h,i);
  bool result;
  try{result=finish_original(o,h,i);}
  catch(...){if(opened)trace->AfterFinish(o,false);throw;}
  if(opened)trace->AfterFinish(o,result);
  return result;
 }
 return s->Finish(o,h,i,finish_original,nullptr);
}
Object AllocateProxy(void*o,void*old,void*parent,void*cache,uint16_t index){
 auto*s=Session::Current();if(!s||!s->OwnsClassOwner(o)){
  if(auto*observer=CalibrationScope::Current())return {reinterpret_cast<uintptr_t>(observer->Allocate(o,old,parent,cache,index,allocate_original))};
  return allocate_original(o,old,parent,cache,index);
 }
 // The common rooted allocator derives the selected parent from the staged
 // public graph. The original parent's source Handle is not graph authority.
 return {reinterpret_cast<uintptr_t>(s->Allocate(o,old,cache,index,nullptr))};
}
void LoadProxy(void*l,void*self,const void*d,const void*def,const void*h){
 // Service owns both ordinary calibration and current Batch early capture.
 installed_service->LoadClass(l,self,d,def,h);
}
bool IdsProxy(const void*h){return installed_service->EnsureInstanceIds(h);}
}
Session::Session(const Bindings&b):bindings_(b),previous_(current){
 if(current||!b.driver||!b.context.env||!b.context.self||!b.hooks||!b.metadata||!b.roots||!b.pool||!b.authority||!b.allocation||!b.access||!b.refuse||!native_hierarchy_source_checks::Ready(b.failure_observer)||
    !b.authority->Staged()||!b.pool->UsesService(*b.roots)||!b.pool->ReadyForSourceAccess())return;
 if(!Mapped(b.driver,sizeof(abi::arm64::android_common::RedefinerPrefix)))return;
 abi::arm64::android_common::RedefinerPrefix driver;std::memcpy(&driver,b.driver,sizeof(driver));
 if(!driver.environment||driver.self!=b.context.self||driver.type!=0||driver.result!=JVMTI_ERROR_INTERNAL)return;
 checks_=std::make_unique<native_hierarchy_source_checks::Scope>(b.driver,b.context,*b.roots,*b.authority,b.refuse);
 ready_=true;current=this;
}
Session::~Session(){
 // Retiring after Run's local Holder/DEX cleanup would be a use-after-free.
 // The owner must pair ReleaseAfterRun; fallible proxies refund before return.
 if(ready_&&!released_)std::abort();
}
Session* Session::Current()noexcept{return current;}
bool Session::Ready()const noexcept{return ready_&&!failed_&&!released_&&current==this;}
bool Session::OwnsDriver(const void*d)const noexcept{return ready_&&!released_&&current==this&&d==bindings_.driver;}
bool Session::OwnsClassOwner(const void*owner)const noexcept{
 if(!Mapped(owner,sizeof(abi::arm64::android_common::ClassRedefinitionPrefix)))return false;
 abi::arm64::android_common::ClassRedefinitionPrefix d;std::memcpy(&d,owner,sizeof(d));return OwnsDriver(d.driver);
}
void Session::Refuse(void*o,int code,const char*literal)noexcept{if(bindings_.refuse)bindings_.refuse(o,code,literal);}
bool Session::Match(void*owner,const void*raw,transaction_root::Iterator*out)const noexcept{
 if(!holder_||!Mapped(raw,sizeof(transaction_root::Iterator))||!transaction_root::ReadIterator(raw,sizeof(transaction_root::Iterator),out)||out->holder!=holder_||size_t(out->index)>=metadata_.count)return false;
 auto*expected=metadata_.owners.begin+metadata_.stride*size_t(out->index);
 if(owner!=expected||!Mapped(owner,sizeof(abi::arm64::android_common::ClassRedefinitionPrefix)))return false;
 abi::arm64::android_common::ClassRedefinitionPrefix d;std::memcpy(&d,owner,sizeof(d));return d.driver==bindings_.driver;
}
bool Session::Finish(void*owner,transaction_root::Holder*holder,void*iter,FinishFunction original,std::string*error){
 if(!Ready()||!original||finishing_||verification_owner_)return false;
 try{
  if(!holder_){
   source_allocation_transaction::Snapshot snapshot;
   if(!source_allocation_transaction::Read(*bindings_.hooks,*bindings_.metadata,holder,*bindings_.pool,&snapshot,error)){
    Refuse(owner,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Source allocation Holder proof failed");Refund();return false;
   }
   holder_=holder;metadata_=snapshot.metadata;
   batch_=std::make_unique<native_hierarchy_allocator::Batch>(*bindings_.allocation,bindings_.context,*bindings_.authority,*bindings_.pool);
   if(bindings_.ledger)batch_->SetStoppedReader(bindings_.ledger);
   if(!batch_->Prepare(holder,error)||!(attached_=bindings_.access->Attach(*batch_))){
    Refuse(owner,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Source allocation Batch preparation failed");Refund();return false;
   }
  }
  transaction_root::Iterator actual{};
  if(holder!=holder_||!Match(owner,iter,&actual)||size_t(actual.index)!=finish_next_){
   Refuse(owner,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Source Finish order or owner changed");Refund();return false;
  }
  auto*trace=native_source_class_loading::Run::Current();
  const bool observed=trace&&trace->OwnsDriver(bindings_.driver);
  if(observed&&!trace->BeforeFinish(owner,holder,iter)){
   Refuse(owner,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Original source class-loading pause was not observed");Refund();return false;
  }
  if(bindings_.ledger&&finish_next_==0&&!bindings_.ledger->AttachHolder(holder_,metadata_)){
   if(observed)trace->AfterFinish(owner,false);
   Refuse(owner,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Source private ledger lacks exact paused Holder");Refund();return false;
  }
  finishing_=owner;
  bool result=false;
  try{result=original(owner,holder,iter);}catch(...){if(observed)trace->AfterFinish(owner,false);throw;}
  finishing_=nullptr;if(observed)trace->AfterFinish(owner,result);
  if(!result||failed_){Refund();return false;}
  ++finish_next_;return true;
 }catch(const std::bad_alloc&){
  finishing_=nullptr;Refuse(owner,JVMTI_ERROR_OUT_OF_MEMORY,"Source hierarchy preparation exhausted native memory");Refund();return false;
 }
}
void* Session::Allocate(void*owner,const void*old,const void*cache,uint16_t index,std::string*error){
 if(!Ready()||!batch_||finishing_!=owner){Refuse(owner,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,"Allocator escaped its exact source Finish owner");Refund();return nullptr;}
 auto*value=batch_->Allocate(owner,old,cache,index,error);
 if(!value)Refund();
 return value;
}
bool Session::BeforeVerification(void*owner,const void*iter,std::string*error){
 transaction_root::Iterator actual{};
 if(!Ready()||!batch_||finishing_||verification_owner_||finish_next_!=metadata_.count||!Match(owner,iter,&actual)||size_t(actual.index)!=verify_next_){
  Error(error,"Verification requires the complete exact source allocation sequence");return false;
 }
 verification_owner_=owner;verification_iterator_=iter;return true;
}
void Session::AfterVerification(void*owner,const void*iter,bool result)noexcept{
 if(!verification_owner_)return; // Before refused before opening a source view.
 if(verification_owner_!=owner||verification_iterator_!=iter)std::abort();
 verification_owner_=nullptr;verification_iterator_=nullptr;
 if(result&&!failed_)++verify_next_;else Refund();
}
bool Session::PrepareCollected(const source_commit::Plan&p,std::string*error){
 if(!Ready()||verification_owner_||finish_next_!=metadata_.count||verify_next_!=metadata_.count){Error(error,"Collected graph lacks complete original source verification");return false;}
 if(bindings_.ledger&&!bindings_.ledger->Collected(p)){Error(error,"Collected private DEX ledger differs from the exact source Plan or paused owner");return false;}
 return true;
}
bool Session::Collected(const source_commit::Plan&p,std::string*error){
 if(!PrepareCollected(p,error))return false;
 if(!bindings_.authority->Seal(p,error))return false;
 sealed_=true;return true;
}
void Session::Refund()noexcept{
 if(released_||committed_)std::abort();
 failed_=true;
 // RecordFailure may run under original Allocate Class monitor or verifier
 // views. Mark failure now; dispose private ownership only after it unwinds.
 if(finishing_||verification_owner_||refunded_)return;
 if(batch_){
  const char* refusal=nullptr;
  const bool discarded=bindings_.ledger?batch_->DiscardPrivate(*bindings_.ledger,&refusal):batch_->Complete(false,nullptr);
  if(!discarded){
   const char* detail=refusal?refusal:"source-temporary-retirement-refused";
#if defined(__ANDROID__)
   __android_log_print(ANDROID_LOG_ERROR,"artdcevm","Owned source refund refused: %s",detail);
#else
   std::fprintf(stderr,"Owned source refund refused: %s\n",detail);
#endif
   std::abort();
  }
 }
 if(bindings_.ledger)bindings_.ledger->Refunded();
 refunded_=true;
 if(bindings_.refund_complete)bindings_.refund_complete(bindings_.refund_owner);
}
void Session::BeginCommit(const source_commit::Plan&p)noexcept{
 if(!Ready()||!sealed_||verification_owner_)std::abort();
 for(size_t i=0;i<p.old_classes.size();++i)if(!bindings_.authority->Verify(p,i))std::abort();
 bindings_.authority->BeginCommit(p);committed_=true;
}
bool Session::ReleaseAfterRun(bool success)noexcept{
 if(!ready_)return false;
 if(released_||current!=this||finishing_||verification_owner_)std::abort();
 // A Run failure before the first allocation has no private captured objects.
 // Closing this empty observer needs no source memory or retirement.
 if(!success&&!failed_&&!batch_){failed_=true;refunded_=true;}
 const bool valid=success?committed_:(failed_&&refunded_);
 // No late refund is attempted. The original source already destroyed Holder.
 if(!valid)std::abort();
 if(committed_&&(!batch_||!batch_->Complete(true,nullptr)))std::abort();
 if(attached_&&!bindings_.access->Detach(*batch_))std::abort();
 if(bindings_.ledger&&!bindings_.ledger->ReleaseAfterRun(success))std::abort();
 batch_.reset();holder_=nullptr;metadata_={};checks_.reset();current=previous_;released_=true;
 return valid;
}
void Session::LoadClass(void*l,void*self,const void*d,const void*def,const void*h)noexcept{bindings_.allocation->LoadClass(l,self,d,def,h);}
bool Session::EnsureInstanceIds(const void*h)noexcept{return bindings_.allocation->EnsureInstanceIds(h);}
CalibrationScope::CalibrationScope(void*driver,const runnable_jni::Context&context,jobject fixture,source_handle_roots::Service&roots,native_hierarchy_allocator::Service&allocation)
 :driver_(driver),context_(context),fixture_(fixture),roots_(roots),allocation_(allocation),previous_(calibration){
 if(current||calibration||!fixture||!context.env||!context.self||!Mapped(driver,sizeof(abi::arm64::android_common::RedefinerPrefix)))return;
 abi::arm64::android_common::RedefinerPrefix d;std::memcpy(&d,driver,sizeof(d));
 if(!d.environment||d.self!=context.self||d.type!=0||d.result!=JVMTI_ERROR_INTERNAL)return;
 calibration=this;active_=true;
}
CalibrationScope::~CalibrationScope(){if(active_){if(calibration!=this)std::abort();calibration=previous_;}}
CalibrationScope*CalibrationScope::Current()noexcept{return calibration;}
void* CalibrationScope::Allocate(void*owner,const void*old,const void*parent,const void*cache,uint16_t index,AllocateFunction original){
 if(!original)return nullptr;
 if(!active_||!Mapped(owner,sizeof(abi::arm64::android_common::ClassRedefinitionPrefix)))return reinterpret_cast<void*>(original(owner,const_cast<void*>(old),const_cast<void*>(parent),const_cast<void*>(cache),index).reference);
 abi::arm64::android_common::ClassRedefinitionPrefix d;std::memcpy(&d,owner,sizeof(d));
 // The independent public/actual Handle oracle selects only the owned fixture.
 // Other classes, owners and source episodes preserve the original trampoline.
 if(d.driver!=driver_||!roots_.ObserveAllocatorHandle(context_,fixture_,old,nullptr))return reinterpret_cast<void*>(original(owner,const_cast<void*>(old),const_cast<void*>(parent),const_cast<void*>(cache),index).reference);
 return allocation_.ObserveOriginal(context_,fixture_,owner,old,parent,cache,index,nullptr);
}
bool InstallPauseObservation(HookEngine&hooks,std::string*error){
 std::lock_guard<std::mutex>lock(installation);
 auto*target=hooks.FindSymbol("libopenjdkjvmti.so",abi::arm64::android_common::kFinishSubtreeAllocations);
 if(!target){Error(error,"Missing named first-Finish pause boundary");return false;}
 if(finish_token){if(target==finish_target)return true;Error(error,"First-Finish pause boundary differs from installed owner");return false;}
 auto*token=hooks.Install(target,reinterpret_cast<void*>(FinishProxy),reinterpret_cast<void**>(&finish_original),error);
 if(!token){finish_original=nullptr;return false;}
 if(!finish_original)std::abort();finish_target=target;finish_token=token;return true;
}
bool Install(HookEngine&hooks,native_hierarchy_allocator::Service&service,const native_hierarchy_allocator::Functions&supplied,std::string*error){
 std::lock_guard<std::mutex>lock(installation);
 if(installed_service){if(installed_service==&service)return true;Error(error,"Hierarchy capture hooks have a different service owner");return false;}
 if(!supplied.find||!supplied.failure||!supplied.retire_status){Error(error,"Source lookup/failure/retirement bindings are mandatory");return false;}
 using namespace abi::arm64::android_common; using namespace ::artdcevm::abi::common;
 const char*libs[]={"libopenjdkjvmti.so","libopenjdkjvmti.so","libart.so","libart.so"};
 const char*names[]={kFinishSubtreeAllocations,kHierarchyAllocator,kHierarchyLoadClass,kCaptureLinkedClass};
 void*proxies[]={reinterpret_cast<void*>(FinishProxy),reinterpret_cast<void*>(AllocateProxy),reinterpret_cast<void*>(LoadProxy),reinterpret_cast<void*>(IdsProxy)};
 void**originals[]={reinterpret_cast<void**>(&finish_original),reinterpret_cast<void**>(&allocate_original),reinterpret_cast<void**>(&load_original),reinterpret_cast<void**>(&ids_original)};
 void*targets[4]{};void*tokens[4]{};
 for(size_t i=0;i<4;++i)if(!(targets[i]=hooks.FindSymbol(libs[i],names[i]))){Error(error,"Missing named source hierarchy allocation boundary");return false;}
 const bool existing_finish=finish_token!=nullptr;
 if(existing_finish&&targets[0]!=finish_target){Error(error,"Hierarchy first-Finish differs from installed pause owner");return false;}
 size_t count=0;
 for(;count<4;++count){if(count==0&&existing_finish)continue;tokens[count]=hooks.Install(targets[count],proxies[count],originals[count],error);if(!tokens[count])break;if(!*originals[count])std::abort();}
 bool bound=false;
 if(count==4){auto functions=supplied;functions.allocate=allocate_original;functions.load_class=load_original;functions.ensure_ids=ids_original;bound=service.SetOriginals(functions,error);}
 if(!bound){while(count){--count;if(tokens[count]&&!hooks.Remove(tokens[count],nullptr))std::abort();}if(!existing_finish)finish_original=nullptr;allocate_original=nullptr;load_original=nullptr;ids_original=nullptr;return false;}
 if(!existing_finish){finish_token=tokens[0];finish_target=targets[0];}
 installed_service=&service;return true;
}
}
