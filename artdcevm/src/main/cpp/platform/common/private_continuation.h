#pragma once
#include <jni.h>
#include <jvmti.h>
#include <cstdint>
#include <string>
#include <atomic>
#include "platform/abi/arm64/android13/continuation.h"
#include "platform/abi/arm64/common/runtime_objects.h"
namespace artdcevm::private_continuation {
extern std::atomic<bool> pc_active, direct_pc_active;
inline bool PcRequired(const void* accessor, const void* frame, uint32_t pc) {
  if (!pc_active.load(std::memory_order_acquire)) return false;
  const auto* f = static_cast<const abi::arm64::android13::ShadowFrame*>(frame);
  if (!(static_cast<const abi::arm64::ArtMethodPrefix*>(f->method)->access_flags.load(std::memory_order_relaxed) & abi::arm64::kAccObsoleteMethod)) return false;
  const auto* a = static_cast<const abi::arm64::android13::CodeItemDataAccessor*>(accessor);
  if (pc > a->size || a->size - pc < 3) return false;
  const uint8_t op = a->insns[pc] & 0xff;
  return op == 0x6e || op == 0x74 || op == 0x72 || op == 0x78 ||
      ((op == 0x70 || op == 0x76) && direct_pc_active.load(std::memory_order_relaxed));
}
// Shared invocation services retain ownership of Class/Thread/collector proofs.
struct Services {
  void* (*resolve)(void*, uint32_t, void*, int);
  bool (*own)(void*, void*);
  bool (*kind)(void*, void*, uint32_t, int);
  void** (*exception)(void*);
  void (*throw_exception)(void*, const char*, const char*);
  bool (*ready)();
  bool (*identity_proof)(void*, void*, const char*, uintptr_t) = nullptr;
  bool (*direct_proof)() = nullptr;
  bool (*direct)(void*, void*, bool completed) = nullptr;
};
void Configure(Services services);
// Borrow the engine environment without replacing any callbacks.
void ConfigureEnvironment(jvmtiEnv* env, bool callbacks_ready);
bool Prepare(JavaVM* vm, JNIEnv* jni);
bool Prove(JNIEnv* jni);
bool Proven();
bool DeviceValidated();
bool Admitted();
bool ProofActive();
#if defined(ARTDCEVM_INTERFACE_ABORT_TESTS)
// An unvalidated device may exercise only engine-owned proof transactions.
bool PrepareForProof(JavaVM* vm, JNIEnv* jni);
#endif
bool Prepared();
// Invalidate only enhancement decisions, at ART publication and ClassPrepare.
void Invalidate();
bool Active();
const char* Failure();
// Only the engine-owned probe transaction may test before proof publication.
bool ProbeTransaction(const std::string& descriptor);
bool Event(void*, const void*, const void*, uint32_t, const void*, void*, bool passed);
bool Exception(void*, void*, bool, bool, bool* handled);
}
