#pragma once
#include <cstdint>
#include <jni.h>
#include <string>
#include <vector>

namespace artdcevm::method_retirement {
struct Hook {
  void* target;
  void* replacement;
  void** original;
  const char* name;
};
// Select a declaration profile whose audited ART build IDs are loaded and
// resolve its hooks before any are installed. Without a matching profile the
// engine keeps Phase 2 and `note` explains why; false means a matched profile
// could not be prepared.
bool Prepare(std::vector<Hook>* hooks, std::string* note);
// A profile is prepared: the engine owns ART's member mapping and can retire methods.
bool Available();
// JNI globals are prepared before entering ART; ReplaceReferences updates them
// to the replacement Class whose ClassExt roots the complete obsolete chain.
void RetainRoots(std::vector<jobject> roots);
bool Rooted(JNIEnv* jni, jclass klass);
uint64_t CheckJniRetirementGuardHits();
}  // namespace artdcevm::method_retirement
