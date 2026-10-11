#include "platform/common/source_handle_roots.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/readable_mapping_scope.h"
#include "platform/abi/arm64/android11/thread_state.h"
#include "platform/abi/arm64/android13/thread_state.h"
#include "platform/abi/arm64/common/hierarchy_allocation.h"
#include <array>
#include <cstring>
#include <cstdlib>
#include <new>
namespace artdcevm::source_handle_roots {
namespace {
using namespace abi::arm64::android_common; using namespace ::artdcevm::abi::common;
template<class T>T Read(const void* p,size_t at=0){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
template<class T>void Write(void* p,size_t at,T value){std::memcpy(static_cast<unsigned char*>(p)+at,&value,sizeof(value));}
// A real native allocation can span adjacent readable VM regions (notably
// ASan allocator arenas). Prove every byte, not just the first region. A hole,
// unreadable region or overflow refuses; no object/slot authority comes from it.
bool Mapped(const void* p,size_t n){
 uintptr_t address=reinterpret_cast<uintptr_t>(p);if(!address||!n||n>UINTPTR_MAX-address)return false;
 while(n){size_t extent=0;if(!method_identity::ReadableMappingExtent(reinterpret_cast<void*>(address),&extent)||!extent)return false;
  if(extent>=n)return true;address+=extent;n-=extent;
 }return true;
}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
void Require(bool value){if(!value)std::abort();}
struct Check final:HandleRootVisitor {
 const void* slots[3]{};size_t count=0,seen=0;uint32_t value=0;bool clear_first=false,valid=true;
 void VisitRoots(void***,size_t,const NativeRootInfo&)override{}
 void VisitRoots(uint32_t** roots,size_t n,const NativeRootInfo& info)override{
  for(size_t i=0;i<n;++i)for(size_t own=0;own<count;++own)if(roots[i]==slots[own]){
   if(seen!=own||*roots[i]!=value||info.type!=4||info.thread_id!=0)valid=false;
   ++seen;if(clear_first&&own==0)*roots[i]=0;
  }
 }
};
}
bool Service::Bind(HookEngine& hooks,const modern_field_storage::Service& storage,Service* output,std::string* error){
 return BindImpl(hooks,storage,nullptr,output,error);
}
bool Service::Bind(HookEngine& hooks,const modern_field_storage::Service& storage,const runnable_jni::Context& c,Service* output,std::string* error){
 return BindImpl(hooks,storage,&c,output,error);
}
bool Service::BindImpl(HookEngine& hooks,const modern_field_storage::Service& storage,const runnable_jni::Context* context,Service* output,std::string* error){
 if(!output||output->current_||!storage.Calibrated()||!storage.CardTableOffset())return Fail(error,"Handle roots require the complete independent card-storage oracle");
 Service candidate;candidate.current_=reinterpret_cast<decltype(candidate.current_)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 candidate.visit_=reinterpret_cast<HandleVisitRoots>(hooks.FindSymbol("libart.so",kHandleVisitRoots));
 if(!candidate.visit_)candidate.visit_signed_=reinterpret_cast<HandleVisitRootsSigned>(hooks.FindSymbol("libart.so",kHandleVisitRootsSigned));
 // Use the same fixed named decoder and actual strong-JNI class provenance as
 // the independently calibrated field-storage exclusive-root owner.
 void* decoder=hooks.FindSymbol("libart.so",abi::arm64::android11::kDecodeJObject);
 if(!decoder)decoder=hooks.FindSymbol("libart.so",runnable_jni::GlobalDecoder::SymbolName());
 candidate.decode_=reinterpret_cast<decltype(candidate.decode_)>(decoder);
 if(!candidate.current_||(!candidate.visit_&&!candidate.visit_signed_)||!candidate.decode_||!hooks.FindSymbol("libopenjdkjvmti.so",kHierarchyAllocator))return Fail(error,"Missing named source allocator/current-thread/handle-root visitor");
 candidate.prefix_=storage.CardTableOffset();
 if(candidate.prefix_>SIZE_MAX-sizeof(HandleThreadPrefix))return Fail(error,"Handle source prefix overflows");
 candidate.top_=candidate.prefix_+offsetof(HandleThreadPrefix,top_handle_scope);
 void* self=candidate.current_();
 if(!Mapped(self,candidate.prefix_+sizeof(HandleThreadPrefix)))return Fail(error,"Source Thread prefix is not mapped");
 if(context){
  if(!context->env||context->self!=self||!context->vm||!Mapped(context->env,sizeof(abi::common::JniEnvironmentPrefix)))return Fail(error,"Source roots actual JNI environment/current Thread mismatch");
  const auto jni=Read<abi::common::JniEnvironmentPrefix>(context->env);
  JavaVM* vm=nullptr;
  if(jni.functions!=context->env->functions||jni.self!=self||jni.vm!=context->vm||
     context->env->GetJavaVM(&vm)!=JNI_OK||vm!=context->vm||context->env->ExceptionCheck())return Fail(error,"Source roots independent JNIEnvExt self/VM oracle mismatch");
  candidate.contextual_=true;candidate.vm_=context->vm;
 }else if(Read<void*>(self,candidate.prefix_+offsetof(HandleThreadPrefix,self))!=self)return Fail(error,"Fixed source TLS self does not match the independent named current thread");
 const uint32_t state=__atomic_load_n(static_cast<const uint32_t*>(self),__ATOMIC_RELAXED);
 if((state>>abi::arm64::android13::kStateShift)==abi::arm64::android13::kNative){candidate.shift_=abi::arm64::android13::kStateShift;candidate.runnable_=abi::arm64::android13::kRunnable;}
 else if((state>>abi::arm64::android11::kStateShift)==abi::arm64::android11::kNative){candidate.shift_=abi::arm64::android11::kStateShift;candidate.runnable_=abi::arm64::android11::kRunnable;}
 else return Fail(error,"Handle-root binding requires independently owned ordinary Native JNI");
 output->decode_=candidate.decode_;output->current_=candidate.current_;output->visit_=candidate.visit_;output->visit_signed_=candidate.visit_signed_;output->vm_=candidate.vm_;output->contextual_=candidate.contextual_;output->prefix_=candidate.prefix_;output->top_=candidate.top_;output->shift_=candidate.shift_;output->runnable_=candidate.runnable_;
 output->bound_self_=self;output->bound_prefix_=candidate.prefix_+sizeof(HandleThreadPrefix);if(error)error->clear();return true;
}
bool Service::Runnable(void* self)const{
 if(!self||!current_||current_()!=self)return false;
 // The named current Thread is still the exact binding-time live Thread. Its
 // calibrated readable prefix contains this fixed atomic state word. Reject
 // Native before an OS query; this does not authorize Native TLS/root access.
 if(self==bound_self_&&bound_prefix_>=sizeof(uint32_t)&&
    (__atomic_load_n(static_cast<const uint32_t*>(self),__ATOMIC_RELAXED)>>shift_)!=runnable_)return false;
 return Mapped(self,prefix_+sizeof(HandleThreadPrefix))&&
  (contextual_||Read<void*>(self,prefix_+offsetof(HandleThreadPrefix,self))==self)&&
  (__atomic_load_n(static_cast<const uint32_t*>(self),__ATOMIC_RELAXED)>>shift_)==runnable_;
}
bool Service::Environment(const runnable_jni::Context& c)const noexcept{
 if(!c.env)return false;
 if(!contextual_)return Read<void*>(c.self,prefix_+offsetof(HandleThreadPrefix,jni_env))==c.env;
 if(c.vm!=vm_||!Mapped(c.env,sizeof(abi::common::JniEnvironmentPrefix)))return false;
 const auto p=Read<abi::common::JniEnvironmentPrefix>(c.env);
 return p.functions==c.env->functions&&p.self==c.self&&p.vm==vm_;
}
void Service::Visit(void* self,HandleRootVisitor* visitor)const{
 if(visit_)visit_(self,visitor,uint32_t{0});else visit_signed_(self,visitor,int32_t{0});
}
bool Service::Contains(void* self,const void* slot,uint32_t value,size_t references,void** owner)const{
 if(!references)references=references_;
 if(references!=sizeof(HandleScopeHeader)&&references!=sizeof(SizedHandleScopeHeader))return false;
 // Validate the entire live chain, including its tail after finding the slot.
 // Brent cycle detection has constant storage and no inventory-size ceiling;
 // a long owned Pool is not a cycle. Every traversed source header/storage is
 // still independently bounded before its link or roots are used.
 void* scope=Read<void*>(self,top_);void* checkpoint=scope;
 size_t power=1,distance=0;bool found=false,variable=false;
 {
 // This snapshot belongs only to the immediate header/slot traversal. There
 // is no JNI, allocation of managed objects or original ART callee here; it
 // is destroyed before the variable-scope walker or caller can safepoint.
 method_identity::ScopedReadableMappings mappings;if(!mappings.Ready())return false;
 while(scope){
  if(!Mapped(scope,sizeof(HandleScopeHeader)))return false;
  const auto header=Read<HandleScopeHeader>(scope);
  if(header.count==-1)variable=true; // Local scopes are walked by original ART.
  else{
   if(header.count<0||size_t(header.count)>(SIZE_MAX-references)/4||!Mapped(scope,references+size_t(header.count)*4))return false;
   uint32_t size=uint32_t(header.count);
   if(references==sizeof(SizedHandleScopeHeader))size=Read<uint32_t>(scope,offsetof(SizedHandleScopeHeader,size));
   if(size>uint32_t(header.count))return false;
   const uintptr_t start=uintptr_t(scope)+references,address=uintptr_t(slot);
   if(address>=start&&(address-start)%4==0&&(address-start)/4<size){if(Read<uint32_t>(slot)!=value)return false;found=true;if(owner)*owner=scope;}
  }
  scope=header.link;
  if(scope&&scope==checkpoint)return false;
  if(++distance==power){
   checkpoint=scope;distance=0;
   if(power>SIZE_MAX/2)return false; // Counter overflow never proves ownership.
   power*=2;
  }
 }
 }
 if(found)return true;
 // A VariableSizedHandleScope owns local scopes outside the Thread's link
 // chain. Only ART's named walker can attest their slot membership here;
 // never infer the variable container's private field geometry. Fixed layout
 // calibration must already have succeeded independently on a source scope.
 if(!variable||!Calibrated()||references!=references_||!value)return false;
 struct Membership final:HandleRootVisitor {
  const void* slot;uint32_t expected;size_t seen=0;bool valid=true;
  Membership(const void* p,uint32_t v):slot(p),expected(v){}
  void VisitRoots(void***,size_t,const NativeRootInfo&)override{}
  void VisitRoots(uint32_t** roots,size_t n,const NativeRootInfo& info)override{
   for(size_t i=0;i<n;++i)if(roots[i]==slot){
    ++seen;if(info.type!=4||info.thread_id!=0||*roots[i]!=expected)valid=false;
   }
  }
 } proof(slot,value);
 Visit(self,&proof);
 // Non-null exact slots must be enumerated once. ART skips null roots, so
 // an unenumerated null variable slot cannot provide membership evidence.
 return proof.valid&&proof.seen==1&&Read<uint32_t>(slot)==value;
}
bool Service::CalibrateLayout(void* self,const void* slot,uint32_t value){
 // Read-only source-layout proof before publishing any owned scope. Every
 // non-null candidate slot must be enumerated by ART, and no other non-null
 // slot in this scope may be enumerated. In particular, the modern size word
 // at +12 is never an ART root; the old first reference at +12 is one.
 struct LayoutCheck final:HandleRootVisitor{
  uintptr_t begin=0,end=0,span_begin=0,span_end=0,next=0;size_t expected=0,seen=0;bool valid=true;
  void Advance(){while(next<end&&!Read<uint32_t>(reinterpret_cast<void*>(next)))next+=4;}
  void VisitRoots(void***,size_t,const NativeRootInfo&)override{}
  void VisitRoots(uint32_t** roots,size_t n,const NativeRootInfo& info)override{
   for(size_t i=0;i<n;++i){const uintptr_t p=uintptr_t(roots[i]);
    if(p<span_begin||p>=span_end||!*roots[i])continue;
    if(info.type!=4||info.thread_id!=0||p<begin||p>=end||(p-begin)%4||p!=next)valid=false;
    ++seen;if(p==next){next+=4;Advance();}
   }
  }
 };
 size_t selected=0;
 for(size_t references:{sizeof(HandleScopeHeader),sizeof(SizedHandleScopeHeader)}){
  void* scope=nullptr;if(!Contains(self,slot,value,references,&scope))continue;
  const auto base=Read<HandleScopeHeader>(scope);const uint32_t size=references==sizeof(HandleScopeHeader)?uint32_t(base.count):Read<uint32_t>(scope,12);
  LayoutCheck check;check.begin=uintptr_t(scope)+references;check.end=check.begin+size_t(size)*4;
  check.span_begin=uintptr_t(scope)+sizeof(HandleScopeHeader);
  check.span_end=check.end; // Includes the old first root or modern size word.
  for(uint32_t i=0;i<size;++i)if(Read<uint32_t>(reinterpret_cast<void*>(check.begin+size_t(i)*4)))++check.expected;
  check.next=check.begin;check.Advance();Visit(self,&check);
  if(!check.valid||check.seen!=check.expected||check.next!=check.end)continue;
  if(selected)return false;selected=references;
 }
 references_=selected;return selected!=0;
}
bool Service::BorrowOwnedStrongGlobal(const runnable_jni::Context& context,jobject strong,void** output)const noexcept{
 if(!output||!decode_||!strong||!Runnable(context.self)||!Environment(context))return false;
 void* expected=reinterpret_cast<void*>(decode_(context.self,strong).reference);
 if(!expected||uintptr_t(expected)>UINT32_MAX)return false;
 *output=expected;return true;
}
bool Service::BorrowOwnedStrongPair(const runnable_jni::Context& context,jobject first,jobject second,void** first_output,void** second_output)const noexcept{
 if(!first_output||!second_output||first_output==second_output||!decode_||!first||!second||!Runnable(context.self)||!Environment(context))return false;
 // The named decoder reads retained JNI globals and barriers only; there is no
 // JNI call, source callee, suspension or reusable permission between decodes.
 void* left=reinterpret_cast<void*>(decode_(context.self,first).reference);
 void* right=reinterpret_cast<void*>(decode_(context.self,second).reference);
 if(!left||!right||uintptr_t(left)>UINT32_MAX||uintptr_t(right)>UINT32_MAX)return false;
 *first_output=left;*second_output=right;return true;
}
bool Service::BorrowStrongGlobal(const runnable_jni::Context& context,jobject strong,void** output)const{
 if(!output||!decode_||!Runnable(context.self)||!Environment(context))return false;
 if(!strong||context.env->ExceptionCheck()||context.env->GetObjectRefType(strong)!=JNIGlobalRefType||!Runnable(context.self))return false;
 void* expected=reinterpret_cast<void*>(decode_(context.self,strong).reference);
 if(!expected||uintptr_t(expected)>UINT32_MAX)return false;
 *output=expected;return true;
}
bool Service::ObserveAllocatorHandle(const runnable_jni::Context& context,jobject strong,const void* slot,std::string* error){
 void* expected=nullptr;
 if(!BorrowStrongGlobal(context,strong,&expected))return Fail(error,"Source allocator original Class is not an actual Runnable owned strong JNI global");
 if(!Mapped(slot,4)||(!references_&&!CalibrateLayout(context.self,slot,uint32_t(uintptr_t(expected))))||!Contains(context.self,slot,uint32_t(uintptr_t(expected))))return Fail(error,"Source Class Handle is not in the independently matched live scope chain");
 Check check;check.slots[0]=slot;check.count=1;check.value=uint32_t(uintptr_t(expected));Visit(context.self,&check);
 if(!check.valid||check.seen!=1)return Fail(error,"Named root visitor does not see the exact public original Class Handle");
 if(!Calibrated()&&!CheckNested(context.self,check.value))return Fail(error,"Named root visitor failed owned nesting/update/restoration proof");
 calibrated_.store(true,std::memory_order_release);if(error)error->clear();return true;
}
bool Service::OrdinaryNative(const runnable_jni::Context& c)const noexcept{
 if(!current_||!shift_||!c.self||current_()!=c.self||!Environment(c)||!Mapped(c.self,sizeof(uint32_t)))return false;
 // BindImpl chose this source shift only by observing actual Native JNI on
 // the independently proven Current/JNIEnvExt owner. No SDK or TLS writes.
 return (__atomic_load_n(static_cast<const uint32_t*>(c.self),__ATOMIC_RELAXED)>>shift_)==abi::arm64::android13::kNative;
}
bool Service::ReadyForSourceAccess()const{return Calibrated()&&current_&&Runnable(current_());}
bool Service::BorrowHandle(const runnable_jni::Context& context,const void* slot,void** output)const {
 if(!output||!Calibrated()||!Runnable(context.self)||!Environment(context)||!Mapped(slot,4))return false;
 const uint32_t value=Read<uint32_t>(slot);
 if(!Contains(context.self,slot,value))return false;
 *output=reinterpret_cast<void*>(uintptr_t(value));return true;
}
std::unique_ptr<Scope> Service::CreateUnchecked(void* self,uint32_t count)const{return std::unique_ptr<Scope>(new Scope(*this,self,count));}
std::unique_ptr<Scope> Service::CreateScope(uint32_t count,std::string* error)const{
 if(!Calibrated()||!Runnable(current_?current_():nullptr)||count>INT32_MAX){Fail(error,"Unproven or non-Runnable owned handle scope");return nullptr;}
 try{return CreateUnchecked(current_(),count);}catch(const std::bad_alloc&){Fail(error,"Cannot allocate owned native handle storage before publication");return nullptr;}
}
std::unique_ptr<Scope> Service::CreateAnchored(Scope& anchor,uint32_t count,std::string* error)const{
 if(&anchor.service_!=this||anchor.anchor_||!Calibrated()||!Runnable(anchor.self_)||
    current_()!=anchor.self_||!count||count>INT32_MAX){Fail(error,"Owned anchor differs from actual calibrated Runnable service");return nullptr;}
 try{return std::unique_ptr<Scope>(new Scope(*this,anchor.self_,count,&anchor));}
 catch(const std::bad_alloc&){Fail(error,"Cannot extend owned native handle anchor");return nullptr;}
}
bool Service::CheckNested(void* self,uint32_t value){
 void* previous=Read<void*>(self,top_);bool valid=true;
 try{
  auto outer=CreateUnchecked(self,3);valid=!outer->Get(0)&&!outer->Get(1)&&!outer->Get(2);
  outer->Set(0,reinterpret_cast<void*>(uintptr_t(value)));outer->Set(2,reinterpret_cast<void*>(uintptr_t(value)));
  {auto inner=CreateUnchecked(self,2);valid&=!inner->Get(0)&&!inner->Get(1);inner->Set(0,reinterpret_cast<void*>(uintptr_t(value)));
   Check check;check.slots[0]=inner->Slot(0);check.slots[1]=outer->Slot(0);check.slots[2]=outer->Slot(2);check.count=3;check.value=value;check.clear_first=true;Visit(self,&check);
   valid&=check.valid&&check.seen==3&&!inner->Get(0)&&!inner->Get(1)&&outer->Get(0)==reinterpret_cast<void*>(uintptr_t(value));
   inner->Set(0,reinterpret_cast<void*>(uintptr_t(value)));check.seen=0;check.clear_first=false;Visit(self,&check);valid&=check.valid&&check.seen==3;
  }
  Check check;check.slots[0]=outer->Slot(0);check.slots[1]=outer->Slot(2);check.count=2;check.value=value;Visit(self,&check);valid&=check.valid&&check.seen==2;
  // A separately created temporary scope stands above the owned anchor. Its
  // saved previous pointer must stay intact while children extend below it.
  {auto temporary=CreateUnchecked(self,1);void* top=Read<void*>(self,top_);
   auto child=std::unique_ptr<Scope>(new Scope(*this,self,1,outer.get()));child->Set(0,reinterpret_cast<void*>(uintptr_t(value)));
   valid&=Read<void*>(self,top_)==top;
   Check anchored;anchored.slots[0]=outer->Slot(0);anchored.slots[1]=outer->Slot(2);anchored.slots[2]=child->Slot(0);anchored.count=3;anchored.value=value;Visit(self,&anchored);
   valid&=anchored.valid&&anchored.seen==3;
   Check moved;moved.slots[0]=child->Slot(0);moved.count=1;moved.value=value;moved.clear_first=true;Visit(self,&moved);
   valid&=moved.valid&&moved.seen==1&&!child->Get(0)&&outer->Get(0)==reinterpret_cast<void*>(uintptr_t(value));
   child->Set(0,reinterpret_cast<void*>(uintptr_t(value)));
   child.reset();valid&=Read<void*>(self,top_)==top;
  }

 }catch(const std::bad_alloc&){valid=false;}
 return valid&&Read<void*>(self,top_)==previous;
}
Scope::Scope(const Service& service,void* self,uint32_t count,Scope* anchor):service_(service),self_(self),count_(count),anchor_(anchor),storage_(nullptr){
 Require(service.Runnable(self)&&count<=INT32_MAX);storage_=std::make_unique<unsigned char[]>(service.references_+size_t(count)*4);
 const HandleScopeHeader header{anchor?Read<HandleScopeHeader>(anchor->storage_.get()).link:Read<void*>(self,service.top_),int32_t(count)};std::memcpy(storage_.get(),&header,sizeof(header));
 if(service.references_==sizeof(SizedHandleScopeHeader))Write<uint32_t>(storage_.get(),12,count);
 if(anchor)Write(anchor->storage_.get(),0,static_cast<void*>(storage_.get()));else Write(self,service.top_,static_cast<void*>(storage_.get()));
}
Scope::~Scope(){
 Require(service_.Runnable(self_));
 if(anchor_){Require(&anchor_->service_==&service_&&anchor_->self_==self_&&Read<HandleScopeHeader>(anchor_->storage_.get()).link==storage_.get());Write(anchor_->storage_.get(),0,Read<HandleScopeHeader>(storage_.get()).link);}
 else{Require(Read<void*>(self_,service_.top_)==storage_.get());Write(self_,service_.top_,Read<HandleScopeHeader>(storage_.get()).link);}
}
void* Scope::Slot(size_t index){Require(index<count_);return storage_.get()+service_.references_+index*4;}
void* Scope::Get(size_t index){Require(service_.Runnable(self_));return reinterpret_cast<void*>(uintptr_t(Read<uint32_t>(Slot(index))));}
void Scope::Set(size_t index,void* object){Require(service_.Runnable(self_)&&uintptr_t(object)<=UINT32_MAX);Write<uint32_t>(Slot(index),0,uint32_t(uintptr_t(object)));}


}
