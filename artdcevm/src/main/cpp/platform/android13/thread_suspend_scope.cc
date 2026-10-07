#include "platform/android13/thread_suspend_scope.h"
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
bool ConfigureThreadSuspendScopes(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
  auto n=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libart.so","artJniMethodStart"));
  auto r=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libart.so","artJniMethodEnd"));
  auto c=reinterpret_cast<void(*)(void*,const char*,bool)>(hooks.FindSymbol("libart.so",thread_suspend_abi::kConstruct));
  auto d=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libart.so",thread_suspend_abi::kDestroy));
  if(!n || !r || !c || !d) { *error="Missing named native transitions or SuspendAll";return false; }
  native=n;runnable=r;construct=c;destroy=d;return true;
}
}
