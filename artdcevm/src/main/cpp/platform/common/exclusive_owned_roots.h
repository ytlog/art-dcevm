#pragma once
#include "api/hook_engine.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
namespace artdcevm::exclusive_owned_roots {
// No JNI environment is exposed during the ART-owned stop. These pointers are
// borrowed only until operation returns; globals stay owned by the caller.
struct Context {void* self;void* const* roots;size_t count;};
using Operation=bool (*)(const Context&,void*);
// Modern pure JNI transition candidate. Actual Native/Runnable state and roots
// must be verified on each call; symbol presence alone publishes no capability.
class Service {
 public:
  static bool Bind(HookEngine&,Service*,std::string*);
  // Call from ordinary Native JNI after the shared executor is initialized.
  // Every nonnull input must be an owned strong global. No JNI, safepoint or
  // retained borrowed pointer is permitted inside operation. A thrown operation
  // is caught after stop release and Runnable restoration by the executor.
  bool With(JNIEnv*,const jobject*,size_t,Operation,void*,std::string*) const;
 private:
  struct Request;
  static bool Enter(const runnable_jni::Context&,void*);
  bool State(void*,uint32_t*)const;
  using Transition=void (*)(void*);
  using Decode=abi::arm64::android11::ReleaseObjectPointer (*)(const void*,jobject);
  abi::arm64::android11::CurrentThread current_=nullptr;
  Transition native_=nullptr,runnable_=nullptr;
  Decode decode_=nullptr;
  abi::arm64::android_common::SuspendConstruct suspend_=nullptr;
  abi::arm64::android_common::SuspendDestroy resume_=nullptr;
  runnable_jni::GlobalDecoder checked_decode_;
};
}
