#pragma once
#include "api/class_redefiner.h"
#include "jvmti.h"
namespace artdcevm::android13 {
using StructuralFn = jvmtiError (*)(jvmtiEnv*, jint, const jvmtiClassDefinition*);
using ModifiableFn = jvmtiError (*)(jvmtiEnv*, jclass, jboolean*);

class JvmtiBodyRedefiner final : public ClassRedefiner {
 public:
  JvmtiBodyRedefiner(jvmtiEnv* env, bool available) : env_(env), available_(available) {}
  bool Available() const override { return available_; }
  int Apply(const std::vector<Definition>& definitions) override;
 private:
  jvmtiEnv* env_;
  bool available_;
};
class ArtAdditiveRedefiner final : public ClassRedefiner {
 public:
  ArtAdditiveRedefiner(jvmtiEnv* env, StructuralFn redefine, ModifiableFn modifiable)
      : env_(env), redefine_(redefine), modifiable_(modifiable) {}
  bool Available() const override { return redefine_ && modifiable_; }
  int Apply(const std::vector<Definition>& definitions) override;
 private:
  jvmtiEnv* env_;
  StructuralFn redefine_;
  ModifiableFn modifiable_;
};
}
