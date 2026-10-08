#include "platform/android13/thread_suspend_scope.h"
#include "platform/common/thread_suspend_service.h"
#include <cstdlib>
namespace artdcevm::android13 {
namespace {
thread_suspend_service::Service service;
}
NativeSuspendScope::NativeSuspendScope(void* self):self_(self),supported_(false),transitioned_(false) {
  supported_=service.EnterNative(self_,sizeof(uint32_t),&transitioned_);
}
NativeSuspendScope::~NativeSuspendScope() {if(transitioned_ && !service.RestoreRunnable(self_)) std::abort();}
OwnedSuspendAll::OwnedSuspendAll(const char* cause,bool long_suspend) {
  if(!service.ConstructSuspend(&storage_,sizeof(storage_),cause,long_suspend)) std::abort();
}
OwnedSuspendAll::~OwnedSuspendAll() {
  if(!service.DestroySuspend(&storage_,sizeof(storage_))) std::abort();
}
bool ConfigureThreadSuspendScopes(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
  thread_suspend_service::Service candidate;
  if(!thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kModern24,
      [&](const char* name){return hooks.FindSymbol("libart.so",name);},&candidate) ||
      !candidate.CanTransitionRunnable()) {
    *error="Missing named native transitions or SuspendAll";return false;
  }
  service=candidate;return true;
}
}
