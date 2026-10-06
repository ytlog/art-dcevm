#include "platform/android13/jvmti_redefiner.h"
namespace artdcevm::android13 {
int JvmtiBodyRedefiner::Apply(const std::vector<Definition>& definitions) {
  if (!Available()) return JVMTI_ERROR_NOT_AVAILABLE;
  std::vector<jvmtiClassDefinition> batch;
  for (const auto& d : definitions) {
    jboolean modifiable = JNI_FALSE;
    auto error = env_->IsModifiableClass(d.klass, &modifiable);
    if (error != JVMTI_ERROR_NONE) return error;
    if (!modifiable) return JVMTI_ERROR_UNMODIFIABLE_CLASS;
    batch.push_back({d.klass, static_cast<jint>(d.dex.size()), d.dex.data()});
  }
  return env_->RedefineClasses(static_cast<jint>(batch.size()), batch.data());
}
int ArtAdditiveRedefiner::Apply(const std::vector<Definition>& definitions) {
  if (!Available()) return JVMTI_ERROR_NOT_AVAILABLE;
  std::vector<jvmtiClassDefinition> batch;
  for (const auto& d : definitions) {
    jboolean modifiable = JNI_FALSE;
    auto error = modifiable_(env_, d.klass, &modifiable);
    if (error != JVMTI_ERROR_NONE) return error;
    if (!modifiable) return JVMTI_ERROR_UNMODIFIABLE_CLASS;
    batch.push_back({d.klass, static_cast<jint>(d.dex.size()), d.dex.data()});
  }
  return redefine_(env_, static_cast<jint>(batch.size()), batch.data());
}
}
