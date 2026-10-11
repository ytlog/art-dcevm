#include "platform/jvmti/public_backend.h"
#include "platform/common/jvmti_extensions.h"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <sys/system_properties.h>

namespace artdcevm::jvmti_backend {
namespace {
std::string Quote(const std::string& value) {
  std::string result = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') result += '\\';
    if (c >= 32) result += c;
  }
  return result + '"';
}
}

int PublicBackend::Attach(JavaVM* vm) {
  if (env_) return JNI_OK;
  char sdk[PROP_VALUE_MAX]{};
  __system_property_get("ro.build.version.sdk", sdk);
  if (vm->GetEnv(reinterpret_cast<void**>(&env_), JVMTI_VERSION_1_2) != JNI_OK) return JNI_ERR;
  sdk_ = sdk;
  jvmtiCapabilities potential{};
  if (env_->GetPotentialCapabilities(&potential) != JVMTI_ERROR_NONE) return JNI_ERR;
  jvmtiCapabilities requested{};
  requested.can_redefine_classes = potential.can_redefine_classes;
  requested.can_retransform_classes = potential.can_retransform_classes;
  if (env_->AddCapabilities(&requested) != JVMTI_ERROR_NONE ||
      env_->GetCapabilities(&caps_) != JVMTI_ERROR_NONE) return JNI_ERR;
  jvmti_extensions::Functions functions(env_);
  if (functions.Load() != JVMTI_ERROR_NONE) return JNI_ERR;
  for (jint i = 0; i < functions.Count(); ++i) {
    const auto& f = functions.At(i);
    if (!f.id) continue;
    extensions_.emplace_back(f.id);
    if (std::strcmp(f.id, "com.android.art.class.structurally_redefine_classes") == 0)
      structural_ = reinterpret_cast<StructuralFn>(f.func);
    if (std::strcmp(f.id, "com.android.art.class.is_structurally_modifiable_class") == 0)
      modifiable_ = reinterpret_cast<ModifiableFn>(f.func);
  }
  return JNI_OK;
}

std::string PublicBackend::Capabilities() const {
  std::ostringstream s;
  s << "{\"backend\":\"public-jvmti-api" << sdk_ << "\",\"attached\":" << (env_ ? "true" : "false")
    << ",\"methodBody\":" << (caps_.can_redefine_classes ? "true" : "false")
    << ",\"nativeAdditive\":" << (structural_ && modifiable_ ? "true" : "false");
  constexpr const char* unavailable[] = {
      "evolutionAvailable", "standardRedefineRouted", "nativeDelete", "fieldTypeEvolution",
      "methodSignatureEvolution", "constructorDeletion", "nativeMethodDeletion",
      "classInitializerDeletion", "memberStorageChange", "nativeImplementationChange",
      "memberModifierChange", "privateMethodTransitionNoSubclass",
      "privatePublicInstanceMethodTransition", "privateFieldVisibilityTransition",
      "memberVisibilityWidening", "memberVisibilityNarrowing",
      "instanceMethodSynchronizationChange", "javaMethodSynchronizationChange",
      "methodFinalTransition", "fieldFinalTransition", "fieldVolatileTransition",
      "classFinalTransition", "abstractMethodTransition", "classAbstractConservativeTransition",
      "combinedAbstractTransition", "hierarchyAddition", "hierarchyNarrowing",
      "hierarchyBatchDependencies", "hierarchyChange", "metadataReclamation",
      "fullDcevmComplete", "privateHooksSupported", "validationHooksInstalled"};
  for (const char* name : unavailable) s << ',' << Quote(name) << ":false";
  s << ",\"evolutionError\":\"No calibrated source engine for this ART build\",\"extensions\":[";
  for (size_t i = 0; i < extensions_.size(); ++i) {
    if (i) s << ',';
    s << Quote(extensions_[i]);
  }
  s << "]}";
  return s.str();
}

int PublicBackend::Apply(Operation op, const std::vector<Definition>& defs) {
  if (!env_) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  if (op == Operation::kEvolve) return JVMTI_ERROR_NOT_AVAILABLE;
  if (op != Operation::kBody && op != Operation::kStructure) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  if (op == Operation::kBody && !caps_.can_redefine_classes) return JVMTI_ERROR_NOT_AVAILABLE;
  if (op == Operation::kStructure && (!structural_ || !modifiable_)) return JVMTI_ERROR_NOT_AVAILABLE;
  std::vector<jvmtiClassDefinition> batch;
  batch.reserve(defs.size());
  for (const auto& d : defs) {
    jboolean modifiable = JNI_FALSE;
    jvmtiError error = op == Operation::kBody
        ? env_->IsModifiableClass(d.klass, &modifiable)
        : modifiable_(env_, d.klass, &modifiable);
    if (error != JVMTI_ERROR_NONE) return error;
    if (!modifiable) return JVMTI_ERROR_UNMODIFIABLE_CLASS;
    batch.push_back({d.klass, static_cast<jint>(d.dex.size()), d.dex.data()});
  }
  return op == Operation::kBody
      ? env_->RedefineClasses(static_cast<jint>(batch.size()), batch.data())
      : structural_(env_, static_cast<jint>(batch.size()), batch.data());
}

std::string PublicBackend::Analyze(JNIEnv*, const std::vector<Definition>&) {
  return "{\"code\":98,\"detail\":\"Schema analysis requires a calibrated ART backend\"}";
}

std::string PublicBackend::ErrorName(int error) const {
  char* name = nullptr;
  if (env_ && env_->GetErrorName(static_cast<jvmtiError>(error), &name) == JVMTI_ERROR_NONE) {
    std::string result(name);
    env_->Deallocate(reinterpret_cast<unsigned char*>(name));
    return result;
  }
  return "ENGINE_ERROR_" + std::to_string(error);
}
}  // namespace artdcevm::jvmti_backend
