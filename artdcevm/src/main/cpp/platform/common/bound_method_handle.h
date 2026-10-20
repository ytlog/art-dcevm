#pragma once
#include <cstdint>
#include <jni.h>
#include <jvmti.h>

namespace artdcevm::bound_method_handle {
// Native snapshot: public JVMTI prepares and independently proves the BindTo entry.
bool Prepare(JNIEnv* jni, jvmtiEnv* env);
enum class EntryMode { kPublicJvmti, kNativeEvent, kNativeFrame, kNativePolicy };
EntryMode Mode();
void Enable();
// Source declaration selected by the existing Class/Thread runtime probe.
void Configure(uint32_t (*access)(void*, bool), bool (*receiver_check)(void*, void*));
void JNICALL Breakpoint(jvmtiEnv* env, JNIEnv* jni, jthread thread,
                        jmethodID method, jlocation location);
}  // namespace artdcevm::bound_method_handle
