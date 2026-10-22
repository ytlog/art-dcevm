#pragma once
#include <cstdint>
#include <atomic>
#include "platform/abi/arm64/common/runtime_objects.h"
#include <functional>
#include <jni.h>
#include <jvmti.h>
#include "platform/common/interpreter_events.h"

namespace artdcevm::bound_method_handle {
extern std::atomic<bool> pc_preparing;
extern "C" { extern std::atomic<void*> artdcevm_bound_entry_method; }
inline bool PcRequired(const void* frame, uint32_t pc) {
  return pc_preparing.load(std::memory_order_acquire) ||
      (pc == 0 && static_cast<const abi::arm64::ShadowFramePrefix*>(frame)->method ==
          artdcevm_bound_entry_method.load(std::memory_order_relaxed));
}
// Native snapshot: public JVMTI prepares and independently proves the BindTo entry.
// Optional existing Class/Thread verifier runs only in the proved Runnable entry.
bool DispatchPc(interpreter_events::Pc original, void* self, const void* accessor, const void* frame,
                uint32_t pc, const void* instrumentation, void* result);
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
