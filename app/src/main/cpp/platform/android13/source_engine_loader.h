#pragma once
#include "api/class_redefiner.h"
#include "jvmti.h"
namespace artdcevm::android13 {
// All mutation and initialization uses the common public engine lock in core/agent.cc.
std::string InitializeSourceEngine(JNIEnv* jni, const char* path, jobject calibration);
bool SourceEngineAvailable();
int RedefineWithSource(jvmtiEnv* env, JavaVM* vm, jint count,
                       const jvmtiClassDefinition* definitions);
class SourceClassRedefiner final : public ClassRedefiner {
 public:
  SourceClassRedefiner(JavaVM* vm, jvmtiEnv* env) : vm_(vm), env_(env) {}
  bool Available() const override { return SourceEngineAvailable(); }
  int Apply(const std::vector<Definition>& definitions) override;
 private:
  JavaVM* vm_;
  jvmtiEnv* env_;
};
}
