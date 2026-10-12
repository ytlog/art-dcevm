#pragma once
#include <cstdint>
#include <jni.h>
#include <string>
#include <vector>
#include "evolution/class_schema.h"
#include "platform/abi/arm64/common/redefinition.h"

namespace artdcevm::method_retirement {
struct Hook {
  void* target;
  void* replacement;
  void** original;
  const char* name;
};
// Resolve the retirement hooks by name. When a name is missing or the NDK map
// layout differs, nothing is added, the engine keeps Phase 2 and `note` says
// why. Whether the fixed declarations fit this ART is decided later by probes.
void Prepare(std::vector<Hook>* hooks, std::string* note);
// The retirement hooks are prepared; VerifyTarget decides per transaction.
bool Available();
// Probe the source-declared class layouts on a redefinition target against
// ART's own lookups and the JVMTI view (`live`); `class_def_access` holds the
// raw ClassDef flags. Runs in CheckClass, before ART can no longer fail. True
// when a layout matches every target probed in the current transaction.
bool VerifyTarget(abi::arm64::ClassRedefinitionPrefix* self,
                  const evolution::ClassSchema& live, uint32_t class_def_access);
// One standard-entry transaction. A transaction whose policy decision needs
// the engine's member mapping fails before ART's no-failure point when a later
// probe disables that mapping.
class TransactionScope {
 public:
  TransactionScope();
  ~TransactionScope();
  TransactionScope(const TransactionScope&) = delete;
  TransactionScope& operator=(const TransactionScope&) = delete;
 private:
  void* state_;
};
void RequireMemberMapping();
// JNI globals are prepared before entering ART; ReplaceReferences updates them
// to the replacement Class whose ClassExt roots the complete obsolete chain.
void RetainRoots(std::vector<jobject> roots);
bool Rooted(JNIEnv* jni, jclass klass);
// "unavailable", "unverified", "verified: <layout>" or "disabled: <reason>".
std::string MemberMappingState();
uint64_t CheckJniRetirementGuardHits();
}  // namespace artdcevm::method_retirement
