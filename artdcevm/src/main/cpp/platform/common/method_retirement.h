#pragma once
#include <atomic>
#include <cstdint>
#include <jni.h>
#include <jvmti.h>
#include <string>
#include <vector>
#include "evolution/class_schema.h"
#include "platform/common/art_hook.h"
#include "platform/abi/arm64/common/redefinition.h"

namespace artdcevm::method_retirement {
// Resolve the retirement hooks by name and install the JNI Call*Method* guards
// through JVMTI's function-table override. When a name is missing, the NDK map
// layout differs or the override fails, no hook is added, members cannot retire
// and `note` says why. Whether the fixed declarations fit this ART is
// decided later by probes.
void Prepare(JavaVM* vm, jvmtiEnv* jvmti, std::vector<Hook>* hooks, std::string* note);
// The retirement hooks are prepared; VerifyTarget decides per transaction.
bool Available();
// Source releases in CandidateOrder; verified_only intersects the current
// transaction's VerifyTarget probes. Native callers use false for ordering.
std::vector<int> CandidateReleases(bool verified_only);
// Classes whose fields stand in for retired fields in reflective Field objects
// (com.github.ytlog.artdcevm.internal.RetiredField / RetiredStaticField).
struct FieldPlaceholders {
  JNIEnv* jni;
  jclass instance_fields;
  jclass static_fields;
};
// After the hooks are installed: resolve the placeholder ArtFields. Field
// retirement stays off, with `note` explaining why, when they are unavailable.
void ResolveFieldPlaceholders(jvmtiEnv* jvmti, const FieldPlaceholders& placeholders,
                              std::string* note);
bool FieldRetirementAvailable();
// The verifier redirect for superclass changes is installed.
bool SuperclassChangeAvailable();
// Probe the source-declared class layouts on a redefinition target against
// ART's own lookups, the class's current DEX and the JVMTI view (`live`). Runs
// in a Runnable validation/admission hook, before ART can no longer fail.
// True when a layout matches
// every target probed in the current transaction.
bool VerifyTarget(abi::arm64::ClassRedefinitionPrefix* self, const evolution::ClassSchema& live);
// Java bits of another loaded class's runtime flags (its ClassDef flags), read
// in CheckClass with every layout VerifyTarget accepted for this transaction.
// False when none is verified or the accepted layouts disagree.
bool ClassAccess(abi::arm64::ClassRedefinitionPrefix* self, jclass klass, uint32_t* access);
// JNI globals are prepared before entering ART; ReplaceReferences updates them
// to the replacement Class whose ClassExt roots the complete obsolete chain.
void RetainRoots(std::vector<jobject> roots);
bool Rooted(JNIEnv* jni, jclass klass);
// "unavailable", "unverified", "verified: <layout>" or "disabled: <reason>".
std::string MemberMappingState();
// JNI and Method.invoke calls rejected because their target was retired.
uint64_t JniRetirementGuardHits();
}  // namespace artdcevm::method_retirement
