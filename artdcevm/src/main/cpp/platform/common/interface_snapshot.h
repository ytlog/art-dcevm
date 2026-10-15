#pragma once
#include <jni.h>
#include <jvmti.h>
#include <cstdint>
#include <string>
#include <vector>
#include <string>

namespace artdcevm::interface_redefinition {
// Borrowed references from the standard entry's GetLoadedClasses result.
// That result must outlive the ART transaction. No raw ART pointers are cached.
struct TypeSnapshot {
  std::vector<jclass> types;
  // Native-captured names for diagnostics in ART hooks without JNI/JVMTI calls.
  std::vector<std::string> descriptors;
  // Index of the affected direct superclass, or -1 for an unaffected parent.
  std::vector<int32_t> superclass;
};
// Native state only. O(loaded classes + selected types + direct hierarchy edges)
// expected work, using Java's identity map rather than pairwise type comparisons.
jvmtiError SnapshotTypes(JNIEnv* jni, jvmtiEnv* env, jclass target, jint count,
                         const jclass* loaded, TypeSnapshot* output, std::string* error);
}  // namespace artdcevm::interface_redefinition
