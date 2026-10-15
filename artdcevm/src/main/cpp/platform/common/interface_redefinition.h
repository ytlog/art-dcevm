#pragma once
#include <jni.h>
#include <jvmti.h>
#include <vector>
#include "platform/common/method_retirement.h"

namespace artdcevm::interface_redefinition {
void Prepare(std::vector<method_retirement::Hook>* hooks, std::string* note);
bool Available();
// Called by the existing named FindClass hooks. ClassLinker is process-lived.
void ObserveClassLinker(void* linker);
// References remain owned by the caller throughout the routed batch.
class Scope {
 public:
  Scope(JNIEnv* jni, jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions);
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
 private:
  void* state_;
};
bool IsTarget(abi::arm64::ClassRedefinitionPrefix* self);
}  // namespace artdcevm::interface_redefinition
