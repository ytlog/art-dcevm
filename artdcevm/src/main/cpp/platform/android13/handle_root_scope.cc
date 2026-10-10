#include "platform/android13/handle_root_scope.h"
#include "platform/abi/arm64/android13/handle_root_scope_abi.h"
#include <cstdlib>
#include <cstring>
#include "platform/abi/arm64/android13/native_object_access_abi.h"
namespace artdcevm::android13 {
namespace {
using Current = void* (*)();
Current current = nullptr;

void Require(bool condition) { if (!condition) std::abort(); }
template<class T> T Read(void* object,size_t at) { T value; std::memcpy(&value,static_cast<unsigned char*>(object)+at,sizeof(value)); return value; }
template<class T> void Write(void* object,size_t at,T value) { std::memcpy(static_cast<unsigned char*>(object)+at,&value,sizeof(value)); }
struct RootCheck final:handle_root_abi::RootVisitor {
 void* slots[3]{};size_t count=0,seen=0;uint32_t reference=0;bool clear_first=false,valid=true;
 void VisitRoots(void***,size_t count,const handle_root_abi::RootInfo&)override{if(count)valid=false;}
 void VisitRoots(uint32_t** roots,size_t n,const handle_root_abi::RootInfo& info)override{
  for(size_t i=0;i<n;++i)for(size_t owned=0;owned<count;++owned)if(roots[i]==slots[owned]){
   if(seen!=owned||*roots[i]!=reference||info.type!=4||info.thread_id!=0)valid=false;
   ++seen;if(clear_first&&owned==0)*roots[i]=0;
  }
 }
};
bool Calibrate(void* self,void* peer,void(*visit)(void*,handle_root_abi::RootVisitor*,uint32_t)) {
 const uintptr_t reference=reinterpret_cast<uintptr_t>(peer);
 if(!self||!peer||reference>UINT32_MAX)return false;
 void* previous=Read<void*>(self,handle_root_abi::kTopHandleScopeOffset);bool valid=true;
 {
  HandleRootScope outer(self,3);
  valid=!outer.Get(0)&&!outer.Get(1)&&!outer.Get(2);
  outer.Set(0,peer);outer.Set(2,peer);
  {
   HandleRootScope inner(self,2);valid&=!inner.Get(0)&&!inner.Get(1);
   inner.Set(0,peer);RootCheck check;check.slots[0]=inner.Slot(0);check.slots[1]=outer.Slot(0);check.slots[2]=outer.Slot(2);
   check.count=3;check.reference=static_cast<uint32_t>(reference);check.clear_first=true;
   visit(self,&check,0);
   valid&=check.valid&&check.seen==3&&!inner.Get(0)&&!inner.Get(1)&&outer.Get(0)==peer&&outer.Get(2)==peer;
   inner.Set(0,peer);check.seen=0;check.clear_first=false;visit(self,&check,0);valid&=check.valid&&check.seen==3;
  }
  RootCheck check;check.slots[0]=outer.Slot(0);check.slots[1]=outer.Slot(2);check.count=2;check.reference=static_cast<uint32_t>(reference);
  visit(self,&check,0);valid&=check.valid&&check.seen==2&&Read<void*>(self,handle_root_abi::kTopHandleScopeOffset)==outer.NativeScope();
 }
 return valid&&Read<void*>(self,handle_root_abi::kTopHandleScopeOffset)==previous;
}
}
void* CurrentHierarchyThread() { return current ? current() : nullptr; }
void ClearHierarchyException(void* thread) { Write<void*>(thread,handle_root_abi::kExceptionOffset,nullptr); }
HandleRootScope::HandleRootScope(void* thread,uint32_t count)
    : thread_(thread), count_(count), storage_(nullptr) {
  static_assert(sizeof(size_t)==8);
  Require(thread != nullptr && count <= INT32_MAX);
  const size_t size=handle_root_abi::kReferencesOffset+size_t{count}*4;
  storage_=std::make_unique<unsigned char[]>(size); // zero every root before publication
  handle_root_abi::ScopeHeader header{Read<void*>(thread,handle_root_abi::kTopHandleScopeOffset),static_cast<int32_t>(count)};
  std::memcpy(storage_.get(),&header,sizeof(header));
  Write(thread,handle_root_abi::kTopHandleScopeOffset,NativeScope());
}
HandleRootScope::~HandleRootScope() {
  Require(Read<void*>(thread_,handle_root_abi::kTopHandleScopeOffset)==NativeScope());
  Write(thread_,handle_root_abi::kTopHandleScopeOffset,Read<void*>(NativeScope(),0));
}
void* HandleRootScope::Slot(size_t index) { Require(index<count_); return storage_.get()+handle_root_abi::kReferencesOffset+index*4; }
void* HandleRootScope::Get(size_t index) { return reinterpret_cast<void*>(static_cast<uintptr_t>(Read<uint32_t>(Slot(index),0))); }
void HandleRootScope::Set(size_t index,void* object) {
  const uintptr_t reference=reinterpret_cast<uintptr_t>(object); Require(reference<=UINT32_MAX);
  Write<uint32_t>(Slot(index),0,static_cast<uint32_t>(reference));
}
bool ConfigureHandleRootScopes(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
 if(!profile.Matched()){*error="Handle-root scopes require a matched device profile";return false;}
 auto c=reinterpret_cast<Current>(hooks.FindSymbol("libart.so",handle_root_abi::kCurrentThread));
 auto visit=reinterpret_cast<void(*)(void*,handle_root_abi::RootVisitor*,uint32_t)>(hooks.FindSymbol("libart.so",handle_root_abi::kVisitRoots));
 auto peer=reinterpret_cast<void*(*)(void*)>(hooks.FindSymbol("libart.so",handle_root_abi::kPeer));
 auto runnable=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libart.so","artJniMethodEnd"));
 auto native=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libart.so","artJniMethodStart"));
 if(!c||!visit||!peer||!runnable||!native){*error="Missing named thread or GC handle-root traversal capability";return false;}
 void* self=c();if(!self){*error="Handle-root calibration requires a current ART thread";return false;}
 uint32_t state=__atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(self)+native_access_abi::kThreadFlagsOffset),__ATOMIC_RELAXED)>>native_access_abi::kStateShift;
 if(state!=native_access_abi::kNative&&state!=native_access_abi::kRunnable){*error="Unsupported handle-root calibration thread state";return false;}
 current=c;
 const bool transitioned=state==native_access_abi::kNative;if(transitioned)runnable(self);
 struct Restore {void* self;void(*leave)(void*);~Restore(){if(leave)leave(self);}} restore{self,transitioned?native:nullptr};
 bool valid=Calibrate(self,peer(self),visit);
 if(!valid){current=nullptr;*error="Named GC root traversal did not preserve owned handle slots";return false;}
 return true;
}
}
