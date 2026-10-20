#pragma once
#include <jni.h>
#include <jvmti.h>

namespace artdcevm::bound_method_handle {
// Native snapshot only: prepare the bootstrap BindTo entry through public JVMTI.
bool Prepare(JNIEnv* jni, jvmtiEnv* env);
void Enable();
void JNICALL Breakpoint(jvmtiEnv* env, JNIEnv* jni, jthread thread,
                        jmethodID method, jlocation location);
}  // namespace artdcevm::bound_method_handle
