#pragma once
#include <string>
#include <vector>
#include <jni.h>
#include <cstdint>
#include "evolution/class_schema.h"
#include "platform/abi/arm64/common/redefinition.h"

namespace artdcevm::method_retirement {
struct Hook {
  void* target;
  void* replacement;
  void** original;
  const char* name;
};
// Resolve the fixed Android 16 ELF profile before any hooks are installed.
// Other builds keep Phase 2 until their declarations/device evidence exist.
bool Prepare(int api, std::vector<Hook>* hooks, std::string* failure);
bool Available();
// JNI globals are prepared before entering ART; ReplaceReferences updates them
// to the replacement Class whose ClassExt roots the complete obsolete chain.
void RetainRoots(std::vector<jobject> roots);
bool Rooted(JNIEnv* jni, jclass klass);
class BatchScope {
 public:
  BatchScope();
  ~BatchScope();
  BatchScope(const BatchScope&) = delete;
  BatchScope& operator=(const BatchScope&) = delete;
 private:
  void* state_;
};
void RegisterRetirements(abi::arm64::ClassRedefinitionPrefix* self,
                         std::vector<evolution::Member> methods);
void InvalidateEnclosingBatches();
uint64_t CheckJniRetirementGuardHits();
}  // namespace artdcevm::method_retirement
