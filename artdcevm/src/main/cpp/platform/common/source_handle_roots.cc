#include "platform/common/source_handle_roots.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android11/thread_state.h"
#include "platform/abi/arm64/android13/thread_state.h"
#include "platform/abi/arm64/android-common/hierarchy_allocation.h"
#include <array>
#include <cstring>
#include <cstdlib>
#include <new>
namespace artdcevm::source_handle_roots {
namespace {
using namespace abi::arm64::android_common;
template<class T>T Read(const void* p,size_t at=0){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
template<class T>void Write(void* p,size_t at,T value){std::memcpy(static_cast<unsigned char*>(p)+at,&value,sizeof(value));}
bool Mapped(const void* p,size_t n){size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=n;}
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
 if(!output||output->current_||!storage.Calibrated()||!storage.CardTableOffset())return Fail(error,"Handle roots require the complete independent card-storage oracle");
 Service candidate;candidate.current_=reinterpret_cast<decltype(candidate.current_)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 candidate.visit_=reinterpret_cast<HandleVisitRoots>(hooks.FindSymbol("libart.so",kHandleVisitRoots));
 // Use the same fixed named decoder and actual strong-JNI class provenance as
 // the independently calibrated field-storage exclusive-root owner.
 void* decoder=hooks.FindSymbol("libart.so",abi::arm64::android11::kDecodeJObject);
 if(!decoder)decoder=hooks.FindSymbol("libart.so",runnable_jni::GlobalDecoder::SymbolName());
 candidate.decode_=reinterpret_cast<decltype(candidate.decode_)>(decoder);
 if(!candidate.current_||!candidate.visit_||!candidate.decode_||!hooks.FindSymbol("libopenjdkjvmti.so",kHierarchyAllocator))return Fail(error,"Missing named source allocator/current-thread/handle-root visitor");
 candidate.prefix_=storage.CardTableOffset();
 if(candidate.prefix_>SIZE_MAX-sizeof(HandleThreadPrefix))return Fail(error,"Handle source prefix overflows");
 candidate.top_=candidate.prefix_+offsetof(HandleThreadPrefix,top_handle_scope);
 void* self=candidate.current_();
 if(!Mapped(self,candidate.prefix_+sizeof(HandleThreadPrefix))||Read<void*>(self,candidate.prefix_+offsetof(HandleThreadPrefix,self))!=self)return Fail(error,"Fixed source TLS self does not match the independent named current thread");
 const uint32_t state=__atomic_load_n(static_cast<const uint32_t*>(self),__ATOMIC_RELAXED);
 if((state>>abi::arm64::android13::kStateShift)==abi::arm64::android13::kNative){candidate.shift_=abi::arm64::android13::kStateShift;candidate.runnable_=abi::arm64::android13::kRunnable;}
 else if((state>>abi::arm64::android11::kStateShift)==abi::arm64::android11::kNative){candidate.shift_=abi::arm64::android11::kStateShift;candidate.runnable_=abi::arm64::android11::kRunnable;}
 else return Fail(error,"Handle-root binding requires independently owned ordinary Native JNI");
 output->decode_=candidate.decode_;output->current_=candidate.current_;output->visit_=candidate.visit_;output->prefix_=candidate.prefix_;output->top_=candidate.top_;output->shift_=candidate.shift_;output->runnable_=candidate.runnable_;if(error)error->clear();return true;
}
bool Service::Runnable(void* self)const{
 return self&&current_&&current_()==self&&Mapped(self,prefix_+sizeof(HandleThreadPrefix))&&
  Read<void*>(self,prefix_+offsetof(HandleThreadPrefix,self))==self&&
  (__atomic_load_n(static_cast<const uint32_t*>(self),__ATOMIC_RELAXED)>>shift_)==runnable_;
}
bool Service::Contains(void* self,const void* slot,uint32_t value)const{
 std::array<const void*,1024> visited{};size_t used=0;bool found=false;
 for(void* scope=Read<void*>(self,top_);scope;scope=Read<HandleScopeHeader>(scope).link){
  if(used==visited.size()||!Mapped(scope,sizeof(HandleScopeHeader)))return false;
  for(size_t i=0;i<used;++i)if(visited[i]==scope)return false;visited[used++]=scope;
  const auto header=Read<HandleScopeHeader>(scope);
  if(header.count==-1)continue; // Fixed VariableSizedHandleScope marker; link remains common.
  if(header.count<0||size_t(header.count)>(SIZE_MAX-kHandleReferences)/4||!Mapped(scope,kHandleReferences+size_t(header.count)*4))return false;
  const uintptr_t start=uintptr_t(scope)+kHandleReferences,address=uintptr_t(slot);
  if(address>=start&&(address-start)%4==0&&(address-start)/4<size_t(header.count)){if(Read<uint32_t>(slot)!=value)return false;found=true;}
 }
 return found;
}
bool Service::ObserveAllocatorHandle(const runnable_jni::Context& context,jobject strong,const void* slot,std::string* error){
 if(!Runnable(context.self)||!context.env||Read<void*>(context.self,prefix_+offsetof(HandleThreadPrefix,jni_env))!=context.env)return Fail(error,"Source allocator witness lacks the actual owned Runnable JNI thread");
 if(!strong||context.env->ExceptionCheck()||context.env->GetObjectRefType(strong)!=JNIGlobalRefType||!Runnable(context.self))return Fail(error,"Source allocator original Class is not an owned strong JNI global");
 void* expected=reinterpret_cast<void*>(decode_(context.self,strong).reference);
 if(!expected||uintptr_t(expected)>UINT32_MAX||!Mapped(slot,4)||!Contains(context.self,slot,uint32_t(uintptr_t(expected))))return Fail(error,"Source Class Handle is not in the independently matched live scope chain");
 Check check;check.slots[0]=slot;check.count=1;check.value=uint32_t(uintptr_t(expected));visit_(context.self,&check,0);
 if(!check.valid||check.seen!=1)return Fail(error,"Named root visitor does not see the exact public original Class Handle");
 if(!Calibrated()&&!CheckNested(context.self,check.value))return Fail(error,"Named root visitor failed owned nesting/update/restoration proof");
 calibrated_.store(true,std::memory_order_release);if(error)error->clear();return true;
}
std::unique_ptr<Scope> Service::CreateUnchecked(void* self,uint32_t count)const{return std::unique_ptr<Scope>(new Scope(*this,self,count));}
std::unique_ptr<Scope> Service::CreateScope(uint32_t count,std::string* error)const{
 if(!Calibrated()||!Runnable(current_?current_():nullptr)||count>INT32_MAX){Fail(error,"Unproven or non-Runnable owned handle scope");return nullptr;}
 try{return CreateUnchecked(current_(),count);}catch(const std::bad_alloc&){Fail(error,"Cannot allocate owned native handle storage before publication");return nullptr;}
}
bool Service::CheckNested(void* self,uint32_t value){
 void* previous=Read<void*>(self,top_);bool valid=true;
 try{
  auto outer=CreateUnchecked(self,3);valid=!outer->Get(0)&&!outer->Get(1)&&!outer->Get(2);
  outer->Set(0,reinterpret_cast<void*>(uintptr_t(value)));outer->Set(2,reinterpret_cast<void*>(uintptr_t(value)));
  {auto inner=CreateUnchecked(self,2);valid&=!inner->Get(0)&&!inner->Get(1);inner->Set(0,reinterpret_cast<void*>(uintptr_t(value)));
   Check check;check.slots[0]=inner->Slot(0);check.slots[1]=outer->Slot(0);check.slots[2]=outer->Slot(2);check.count=3;check.value=value;check.clear_first=true;visit_(self,&check,0);
   valid&=check.valid&&check.seen==3&&!inner->Get(0)&&!inner->Get(1)&&outer->Get(0)==reinterpret_cast<void*>(uintptr_t(value));
   inner->Set(0,reinterpret_cast<void*>(uintptr_t(value)));check.seen=0;check.clear_first=false;visit_(self,&check,0);valid&=check.valid&&check.seen==3;
  }
  Check check;check.slots[0]=outer->Slot(0);check.slots[1]=outer->Slot(2);check.count=2;check.value=value;visit_(self,&check,0);valid&=check.valid&&check.seen==2;
 }catch(const std::bad_alloc&){valid=false;}
 return valid&&Read<void*>(self,top_)==previous;
}
Scope::Scope(const Service& service,void* self,uint32_t count):service_(service),self_(self),count_(count),storage_(nullptr){
 Require(service.Runnable(self)&&count<=INT32_MAX);storage_=std::make_unique<unsigned char[]>(kHandleReferences+size_t(count)*4);
 const HandleScopeHeader header{Read<void*>(self,service.top_),int32_t(count)};std::memcpy(storage_.get(),&header,sizeof(header));Write(self,service.top_,static_cast<void*>(storage_.get()));
}
Scope::~Scope(){Require(service_.Runnable(self_)&&Read<void*>(self_,service_.top_)==storage_.get());Write(self_,service_.top_,Read<HandleScopeHeader>(storage_.get()).link);}
void* Scope::Slot(size_t index){Require(index<count_);return storage_.get()+kHandleReferences+index*4;}
void* Scope::Get(size_t index){Require(service_.Runnable(self_));return reinterpret_cast<void*>(uintptr_t(Read<uint32_t>(Slot(index))));}
void Scope::Set(size_t index,void* object){Require(service_.Runnable(self_)&&uintptr_t(object)<=UINT32_MAX);Write<uint32_t>(Slot(index),0,uint32_t(uintptr_t(object)));}
}
