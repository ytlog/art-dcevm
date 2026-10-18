#pragma once
#include <jvmti.h>
#include <string>
#include <vector>
#include "platform/common/art_hook.h"
#include "platform/common/class_layout.h"

// JBR's FieldCompatibilityChecker/update_fields decisions, executed by ART's
// existing ReplaceObjectReferences visitor. This module never walks the heap.
namespace artdcevm::narrowed_references {
bool Prepare(const class_layout::Services&, std::vector<Hook>*, std::string*);
void Installed(bool);
bool Available();
jvmtiError Capture(jvmtiEnv*, JNIEnv*, jint, const jclass*, std::string*);
bool Plan(const runtime_thread::VerifiedThread&, abi::arm64::ClassRedefinitionPrefix*,
          const abi::arm64::RedefinitionDataIter&, int, std::string*);
class UpdateScope {
 public:
  UpdateScope(abi::arm64::ClassRedefinitionPrefix*, const abi::arm64::RedefinitionDataIter&);
  ~UpdateScope();
 private:
  void* previous_;
};
uint64_t Visits();
uint64_t Clears();
}  // namespace artdcevm::narrowed_references
