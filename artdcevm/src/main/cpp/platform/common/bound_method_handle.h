#pragma once
#include <cstdint>
#include <functional>
#include <jni.h>
#include <jvmti.h>

namespace artdcevm::bound_method_handle {
// Native snapshot: public JVMTI prepares and independently proves the BindTo entry.
// Optional existing Class/Thread verifier runs only in the proved Runnable entry.
bool Prepare(JNIEnv* jni, jvmtiEnv* env, const std::function<bool()>& frame_probe = {});
enum class EntryMode { kPublicJvmti, kNativeEvent, kNativeFrame, kNativePolicy };
EntryMode Mode();
// Preparation-only proof bits: identity, ordinary entry, initial native entry,
// public continuation, native entry reprobe, native continuation, policy, frame.
uint32_t PreparationProofs();
void Enable();
// Source declaration selected by the existing Class/Thread runtime probe.
void Configure(uint32_t (*access)(void*, bool), int (*receiver_check)(void*, void*), bool (*frame_return)(void*, bool) = nullptr);
void JNICALL Breakpoint(jvmtiEnv* env, JNIEnv* jni, jthread thread,
                        jmethodID method, jlocation location);
}  // namespace artdcevm::bound_method_handle
