#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <jni.h>
#include <string>

namespace artdcevm::android13 {
bool ConfigureNativeObjectAccess(HookEngine& hooks, const DeviceProfile& profile, std::string* error);
// The current thread is already Runnable and holds the mutator lock.
void* DecodeRunnableObject(void* thread, jobject object);
// JNI native and already-runnable entrypoints only. Unsupported states are
// detected before any transition, so callers can retain their original scope.
class NativeObjectAccess {
 public:
  explicit NativeObjectAccess(JNIEnv* env);
  ~NativeObjectAccess();
  NativeObjectAccess(const NativeObjectAccess&) = delete;
  NativeObjectAccess& operator=(const NativeObjectAccess&) = delete;
  bool Supported() const { return supported_; }
  void* Self() const { return self_; }
  // Borrowed device ObjPtr: use while this scope is alive and do not keep it
  // across a suspension-capable call without an ART root/handle.
  void* Decode(jobject object) const;
 private:
  void* self_;
  bool supported_;
  bool transitioned_;
};
}
