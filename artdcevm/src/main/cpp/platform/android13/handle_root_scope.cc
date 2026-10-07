#include "platform/android13/handle_root_scope.h"
#include "platform/android13/handle_root_scope_abi.h"
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
namespace artdcevm::android13 {
namespace {
using Current = void* (*)();
Current current = nullptr;
using Probe=int(*)(void*,void*,uint32_t);
Probe probe_profile = nullptr;
void Require(bool condition) { if (!condition) std::abort(); }
template<class T> T Read(void* object,size_t at) { T value; std::memcpy(&value,static_cast<unsigned char*>(object)+at,sizeof(value)); return value; }
template<class T> void Write(void* object,size_t at,T value) { std::memcpy(static_cast<unsigned char*>(object)+at,&value,sizeof(value)); }
int Calibrate(void* self) {
  if(!self || self!=current() || probe_profile(self,nullptr,0)!=0) return 1;
  bool valid=false;
  {
    HandleRootScope roots(self,2);
    valid=probe_profile(self,roots.NativeScope(),2)==0;
  }
  return valid && probe_profile(self,nullptr,0)==0 ? 0 : 2;
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
bool ConfigureHandleRootScopes(HookEngine& hooks,void* implementation,std::string* error) {
  using Calibration=int(*)(int(*)(void*));
  auto c=reinterpret_cast<Current>(hooks.FindSymbol("libart.so",handle_root_abi::kCurrentThread));
  auto probe=reinterpret_cast<Probe>(dlsym(implementation,"ArtDcevmSourceProbeHandleRootScope"));
  auto calibrate=reinterpret_cast<Calibration>(dlsym(implementation,"ArtDcevmSourceCalibrateHandleRootScopes"));
  if(!c || !probe || !calibrate) { *error="Missing named current ART thread or handle-root profile probe"; return false; }
  current=c; probe_profile=probe;
  if(calibrate(Calibrate)!=0) {
    current=nullptr;probe_profile=nullptr;
    *error="Owned handle-root scope did not match device ART";return false;
  }
  return true;
}
}
