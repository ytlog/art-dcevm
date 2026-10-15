#pragma once
#include <jni.h>
#include <jvmti.h>
#include <vector>
#include <cstdint>
#include "platform/common/method_retirement.h"

namespace artdcevm::interface_redefinition {
void Prepare(std::vector<method_retirement::Hook>* hooks, std::string* note);
bool Available();
struct Timing { uint64_t types = 0, snapshot_ns = 0, allocation_ns = 0; };
// Diagnostics for the latest capture on the calling transaction thread.
Timing LastTiming();
std::string RuntimeState();
// Called by the existing named FindClass hooks. ClassLinker is process-lived.
void ObserveClassLinker(void* linker);
// References remain owned by the caller throughout the routed batch.
class Scope {
 public:
  Scope(JNIEnv* jni, jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions);
  ~Scope();
  void Describe(jclass klass, const evolution::ClassSchema& live);
  jvmtiError Capture(jint count, const jclass* loaded, uint64_t loaded_ns, std::string* error);
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
 private:
  void* state_;
};
bool IsTarget(abi::arm64::ClassRedefinitionPrefix* self);
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
// Debug APK fault injection; no test entry points are built into release AARs.
bool InjectInstanceFailure(abi::arm64::ClassRedefinitionPrefix* self,
                           abi::arm64::RedefinitionDataIter* data, bool allocated);
#endif
}  // namespace artdcevm::interface_redefinition
