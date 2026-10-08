#pragma once
#include "platform/abi/arm64/android-common/jni_global_ref.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include "platform/common/runnable_jni_bridge.h"
namespace artdcevm::runnable_jni {
// NewGlobalRef's real JNI implementation owns the Runnable/mutator scope.
// Dispatch through its named AddGlobalRef helper after the original has rooted
// the object. No SOA layout or Thread offset is read. A missing/inlined helper
// remains kLeafNotReached; callers must prove actual dispatch before use.
class GlobalRefBridge {
 public:
  using Leaf=abi::arm64::android_common::AddGlobalRef;
  static const char* SymbolName(){return abi::arm64::android_common::kAddGlobalRef;}
  static bool Bind(void* original,void* current_thread,GlobalRefBridge* output);
  // Input must be a caller-owned strong global, alive throughout this call.
  // A temporary duplicate remains rooted through the callback and is deleted
  // before return, including failed callbacks and Java exceptions. Borrowed
  // managed pointers may not escape without independently owned roots.
  Result Invoke(JNIEnv* env,jobject strong_global,Operation operation,void* data) const;
  jobject OnAddGlobalRef(void* vm,void* self,abi::arm64::android11::ReleaseObjectPointer object) const;
 private:
  struct Request;
  static thread_local Request* active_;
  Leaf original_=nullptr;
  abi::arm64::android11::CurrentThread current_=nullptr;
};
}
