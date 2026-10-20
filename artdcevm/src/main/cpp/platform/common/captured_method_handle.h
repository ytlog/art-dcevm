#pragma once
#include <jni.h>
#include <jvmti.h>

namespace artdcevm::captured_method_handle {
// Only the independently identified BindTo.transform entry may continue an
// already validated capture. The remaining graph runs through libcore itself.
bool Prepare(JNIEnv* jni, jvmtiEnv* env, jmethodID entry);
// Complete the current void BindTo frame; false leaves a pending exception.
bool Complete(JNIEnv* jni = nullptr, jthread thread = nullptr);
}
