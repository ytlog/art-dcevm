#pragma once
#include "api/runtime_backend.h"
#include "jvmti.h"
#include "platform/android13/jvmti_redefiner.h"
#include <memory>
#include "hooks/shadow_hook_engine.h"
#include "platform/android13/validation_hooks.h"

namespace artdcevm::android13 {
class Android13Backend final : public RuntimeBackend {
 public:
  int Attach(JavaVM* vm) override;
  std::string Capabilities() const override;
  int Apply(Operation op, const std::vector<Definition>& defs) override;
  std::string Analyze(JNIEnv* jni, const std::vector<Definition>& defs) override;
  std::string ErrorName(int error) const override;
  int EnableValidationHooks() override;
  int EnableStandardRedefineRouting() override;
  int InitializeEvolution(JNIEnv* jni, const char* module_path, jobject calibration) override;
  jvmtiError RouteRedefinition(jvmtiEnv* caller, jint count,
                                const jvmtiClassDefinition* definitions);
  using RedefineFn = jvmtiError (*)(jvmtiEnv*, jint, const jvmtiClassDefinition*);
  RedefineFn original_redefine() const { return original_redefine_; }
 private:
  jvmtiEnv* env_ = nullptr;
  JavaVM* vm_ = nullptr;
  StructuralFn structural_ = nullptr;
  ModifiableFn modifiable_ = nullptr;
  std::vector<std::string> extensions_;
  jvmtiCapabilities caps_{};
  std::unique_ptr<ClassRedefiner> body_;
  std::unique_ptr<ClassRedefiner> additive_;
  // No fallback to additive when a caller requests full schema evolution.
  std::unique_ptr<ClassRedefiner> evolution_;
  ShadowHookEngine hook_engine_;
  ValidationHooks validation_hooks_;
  std::string hook_error_;
  std::string evolution_error_;
  RedefineFn original_redefine_ = nullptr;
  void* redefine_hook_ = nullptr;
  bool routing_attempted_ = false;
};
}
