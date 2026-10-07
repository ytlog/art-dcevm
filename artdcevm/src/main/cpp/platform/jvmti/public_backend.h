#pragma once
#include "api/runtime_backend.h"
#include "jvmti.h"
#include <string>
#include <vector>

namespace artdcevm::jvmti_backend {
class PublicBackend final : public RuntimeBackend {
 public:
  int Attach(JavaVM* vm) override;
  std::string Capabilities() const override;
  int Apply(Operation op, const std::vector<Definition>& defs) override;
  std::string Analyze(JNIEnv*, const std::vector<Definition>&) override;
  std::string ErrorName(int error) const override;
  int EnableValidationHooks() override { return JVMTI_ERROR_NOT_AVAILABLE; }
  int EnableStandardRedefineRouting() override { return JVMTI_ERROR_NOT_AVAILABLE; }
  int InitializeEvolution(JNIEnv*, jobject) override { return JVMTI_ERROR_NOT_AVAILABLE; }
 private:
  jvmtiEnv* env_ = nullptr;
  jvmtiCapabilities caps_{};
  using StructuralFn = jvmtiError (*)(jvmtiEnv*, jint, const jvmtiClassDefinition*);
  using ModifiableFn = jvmtiError (*)(jvmtiEnv*, jclass, jboolean*);
  StructuralFn structural_ = nullptr;
  ModifiableFn modifiable_ = nullptr;
  std::vector<std::string> extensions_;
  std::string sdk_;
};
}  // namespace artdcevm::jvmti_backend
