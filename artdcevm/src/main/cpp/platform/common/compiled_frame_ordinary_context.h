#pragma once
#include "platform/common/runnable_executor.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
namespace artdcevm::compiled_frame_fixture {
// Ordinary frame calibration precedes schema graph staging. Its owner comes
// from the independently named current-Thread leaf, never a graph-stage field.
inline bool SameOrdinaryOwner(const runnable_jni::Context& context,JNIEnv* env,
    JavaVM* vm,abi::arm64::android11::CurrentThread current) noexcept {
 return context.self&&env&&vm&&current&&context.env==env&&context.vm==vm&&current()==context.self;
}
}
