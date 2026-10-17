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

namespace artdcevm::runtime_thread { class VerifiedThread; }

namespace artdcevm::interface_redefinition {
struct Services {
  bool verifier_redirect;
  bool runtime_thread;
  void* (*mirror_class)(abi::arm64::ClassRedefinitionPrefix*);
  abi::arm64::ArtField* (*find_instance_field)(void*, std::string_view, std::string_view);
};
struct Timing { uint64_t types = 0, snapshot_ns = 0, allocation_ns = 0; };
// One optional backend, assembled by Install. Its hooks borrow the same process
// state; transaction-local data stays in the context's interface Part.
struct Backend {
  virtual ~Backend() = default;
  virtual bool Available() = 0;
  // ART 13 needs its named early-link error service to diagnose non-public targets.
  virtual bool AbstractLinkErrorsAvailable() = 0;
  // Diagnostics for the latest capture on the calling transaction thread.
  virtual Timing LastTiming() = 0;
  virtual std::string RuntimeState() = 0;
  // Format a saved static hook rejection only after returning to Native state.
  virtual std::string TakeRejection() = 0;
  // Called by verification_redirect's named FindClass hooks. ClassLinker is process-lived.
  virtual void ObserveClassLinker(void* linker) = 0;
  // The interface groups of the current transaction context. References remain
  // owned by the caller throughout the routed batch.
  virtual void Begin(JNIEnv* jni, jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) = 0;
  virtual void Describe(jclass klass, const evolution::ClassSchema& live) = 0;
  virtual jvmtiError Capture(jint count, const jclass* loaded, uint64_t loaded_ns, std::string* error) = 0;
  // After Capture: the types of the group that rebuilds interface target
  // `klass` (its owner first, then every prepared subtype), or nullptr when
  // `klass` is not a captured interface target. Borrowed until the context ends.
  virtual const std::vector<jclass>* GroupTypes(jclass klass) = 0;
  virtual bool IsTarget(abi::arm64::ClassRedefinitionPrefix* self) = 0;
  virtual bool IsAffected(abi::arm64::ClassRedefinitionPrefix* self) = 0;
  // A batch class outside every interface target's type relation that is a
  // superclass of a group member. A structural one joins that group's rebuild.
  virtual bool SharesImplementationGroup(abi::arm64::ClassRedefinitionPrefix* self) = 0;
  // Current-thread verification only: lookup by loader without cache writes.
  virtual void* VerificationClass(void* linker, std::string_view descriptor, abi::arm64::Handle loader) noexcept = 0;
  virtual jvmtiError VerificationError() noexcept = 0;
  // After ART's instance collection for an entry: a rebuild whose owner does
  // not cover every member collects the other roots' instances too.
  virtual bool CompleteInstances(abi::arm64::ClassRedefinitionPrefix*, abi::arm64::RedefinitionDataIter*,
                                 bool (*collect)(abi::arm64::ClassRedefinitionPrefix*,
                                                 abi::arm64::RedefinitionDataIter*)) noexcept = 0;
  // Interface dispatch acceptance before ART's no-failure point.
  virtual bool Check(const runtime_thread::VerifiedThread&, abi::arm64::ClassRedefinitionPrefix*,
                     const abi::arm64::RedefinitionDataIter&, std::string*) = 0;
};
// Prepare the module's process-lived state, created on the first call, and
// return its backend only when named services are available. The backend keeps
// its identity across later calls. The unavailable report preserves the
// existing capability text.
Backend* Prepare(const Services&, std::vector<Hook>*, std::string* note,
                 std::string* unavailable_report);
}  // namespace artdcevm::interface_redefinition
