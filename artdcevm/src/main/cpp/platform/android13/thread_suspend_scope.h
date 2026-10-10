#pragma once
#include "platform/android13/device_profile.h"
#include "api/hook_engine.h"
#include "platform/abi/arm64/android13/thread_suspend_scope_abi.h"
#include <string>
namespace artdcevm::android13 {
bool ConfigureThreadSuspendScopes(HookEngine&,const DeviceProfile& profile,std::string* error);
// Runnable -> Native releases shared mutator access before SuspendAll. Native
// callers stay Native. The original supported state is restored on destruction.
class NativeSuspendScope {
 public:
  explicit NativeSuspendScope(void* self);
  ~NativeSuspendScope();
  bool Supported() const { return supported_; }
  NativeSuspendScope(const NativeSuspendScope&)=delete;
  NativeSuspendScope& operator=(const NativeSuspendScope&)=delete;
 private:
  void* self_;
  bool supported_,transitioned_;
};
// Construct after NativeSuspendScope, before acquiring thread_list_lock.
// Destruct after releasing thread_list_lock, before restoring Runnable.
class OwnedSuspendAll {
 public:
  explicit OwnedSuspendAll(const char* cause,bool long_suspend=false);
  ~OwnedSuspendAll();
  OwnedSuspendAll(const OwnedSuspendAll&)=delete;
  OwnedSuspendAll& operator=(const OwnedSuspendAll&)=delete;
 private:
  thread_suspend_abi::SuspendAll storage_{};
};
}
