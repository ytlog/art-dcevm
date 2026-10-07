#include "platform/android13/thread_suspend_scope.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
namespace artdcevm::android13 {
namespace {
void(*native)(void*)=nullptr;
void(*runnable)(void*)=nullptr;
void(*construct)(void*,const char*,bool)=nullptr;
void(*destroy)(void*)=nullptr;
uint32_t State(void* self) {
  return __atomic_load_n(reinterpret_cast<const uint32_t*>(static_cast<unsigned char*>(self)+
      native_access_abi::kThreadFlagsOffset),__ATOMIC_RELAXED)>>native_access_abi::kStateShift;
}
bool Calibrate(void* self,bool(*is_native)(void*)) {
  if(State(self)!=native_access_abi::kRunnable) return false;
  { NativeSuspendScope native_scope(self);
    if(!native_scope.Supported() || !is_native(self)) return false;
    { NativeSuspendScope nested(self);if(!nested.Supported() || !is_native(self)) return false; }
    { OwnedSuspendAll suspend("ART DCEVM owned suspension calibration"); }
    if(!is_native(self)) return false;
  }
  return State(self)==native_access_abi::kRunnable;
}
}
NativeSuspendScope::NativeSuspendScope(void* self):self_(self),supported_(false),transitioned_(false) {
  if(!self_) return;
  uint32_t state=State(self_);
  supported_=state==native_access_abi::kNative || state==native_access_abi::kRunnable;
  if(supported_ && state==native_access_abi::kRunnable) { native(self_);transitioned_=true; }
}
NativeSuspendScope::~NativeSuspendScope() { if(transitioned_) runnable(self_); }
OwnedSuspendAll::OwnedSuspendAll(const char* cause,bool long_suspend) { construct(&storage_,cause,long_suspend); }
OwnedSuspendAll::~OwnedSuspendAll() { destroy(&storage_); }
bool ConfigureThreadSuspendScopes(HookEngine& hooks,void* implementation,std::string* error) {
  auto n=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libart.so","artJniMethodStart"));
  auto r=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libart.so","artJniMethodEnd"));
  auto c=reinterpret_cast<void(*)(void*,const char*,bool)>(hooks.FindSymbol("libart.so",thread_suspend_abi::kConstruct));
  auto d=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libart.so",thread_suspend_abi::kDestroy));
  using Probe=bool(*)(bool(*)(void*,bool(*)(void*)));
  auto probe=reinterpret_cast<Probe>(dlsym(implementation,"ArtDcevmSourceProbeThreadSuspendScopes"));
  if(!n || !r || !c || !d || !probe) { *error="Missing named native transitions, SuspendAll or profile calibration";return false; }
  native=n;runnable=r;construct=c;destroy=d;
  if(!probe(Calibrate)) { *error="Owned thread suspension scope failed source declaration calibration";return false; }
  return true;
}
}
