#pragma once
#include <jni.h>
#include <jvmti.h>
#include <vector>
#include <cstdint>
#include <string>
#include <string_view>
#include "evolution/class_schema.h"
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/common/art_hook.h"

namespace artdcevm::interface_redefinition {
// `verifier_redirect`: verification_redirect can show ART's verifier the
// group's replacements (member_mapping::SuperclassChangeAvailable).
void Prepare(bool verifier_redirect, std::vector<Hook>* hooks, std::string* note);
bool Available();
struct Timing { uint64_t types = 0, snapshot_ns = 0, allocation_ns = 0; };
// Diagnostics for the latest capture on the calling transaction thread.
Timing LastTiming();
std::string RuntimeState();
// Format a saved static hook rejection only after returning to Native state.
std::string TakeRejection();
// Called by verification_redirect's named FindClass hooks. ClassLinker is process-lived.
void ObserveClassLinker(void* linker);
// The interface groups of the current transaction context. References remain
// owned by the caller throughout the routed batch.
void Begin(JNIEnv* jni, jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions);
void Describe(jclass klass, const evolution::ClassSchema& live);
jvmtiError Capture(jint count, const jclass* loaded, uint64_t loaded_ns, std::string* error);
// After Capture: the types of the group that rebuilds interface target
// `klass` (its owner first, then every prepared subtype), or nullptr when
// `klass` is not a captured interface target. Borrowed until the context ends.
const std::vector<jclass>* GroupTypes(jclass klass);
bool IsTarget(abi::arm64::ClassRedefinitionPrefix* self);
bool IsAffected(abi::arm64::ClassRedefinitionPrefix* self);
bool SharesImplementationGroup(abi::arm64::ClassRedefinitionPrefix* self);
// Current-thread verification only: lookup by loader without cache writes.
void* VerificationClass(void* linker, std::string_view descriptor, abi::arm64::Handle loader) noexcept;
jvmtiError VerificationError() noexcept;
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
// Debug APK fault injection; no test entry points are built into release AARs.
bool InjectInstanceFailure(abi::arm64::ClassRedefinitionPrefix* self,
                           abi::arm64::RedefinitionDataIter* data, bool allocated);
#endif
}  // namespace artdcevm::interface_redefinition
