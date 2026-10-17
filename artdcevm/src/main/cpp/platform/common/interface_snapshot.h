#pragma once
#include <jni.h>
#include <jvmti.h>
#include <cstdint>
#include <string>
#include <vector>

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
// Native state only. Positions in `loaded`, ascending, of every prepared
// non-erroneous loaded type assignable to `root`, including `root` itself.
// An interface root rejects loaded Proxy implementations.
jvmtiError SelectTypes(JNIEnv* jni, jvmtiEnv* env, jclass root, jint count, const jclass* loaded,
                       std::vector<jint>* selected, std::string* error);
// Native state only. Orders the selected loaded types so that each affected
// superclass and interface precedes its descendants, with `first` leading.
// O(selected types + direct hierarchy edges) expected work, using Java's
// identity map rather than pairwise type comparisons.
jvmtiError OrderTypes(JNIEnv* jni, jvmtiEnv* env, jclass first, const jclass* loaded,
                      const std::vector<jint>& selected, TypeSnapshot* output, std::string* error);
}  // namespace artdcevm::interface_redefinition
