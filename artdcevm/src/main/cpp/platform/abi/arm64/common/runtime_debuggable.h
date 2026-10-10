#pragma once
#include <jni.h>
namespace artdcevm::abi::arm64::android_common {
// Fixed11/12/13/14/15/16 releases and17 b753cf9:
// runtime/native/dalvik_system_VMRuntime.cc. The original leaf returns
// Runtime::Current()->IsJavaDebuggable() and does not access its JNI receiver.
// Observing this policy is not an inline/frame capability or graph oracle.
using JavaDebuggableQuery=jboolean (*)(JNIEnv*,jobject);
inline constexpr char kJavaDebuggableQuery[]=
    "_ZN3artL26VMRuntime_isJavaDebuggableEP7_JNIEnvP8_jobject";
}
