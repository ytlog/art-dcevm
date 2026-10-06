#include "platform/android13/android13_backend.h"
#include <android/log.h>
#include "support/elf_modules.h"
#include "support/json.h"
#include "platform/android13/evolution_planner.h"
#include "platform/android13/source_engine_loader.h"
#include "platform/android13/builds/cas_tkq1_221114_001.h"
#include "core/transaction_lock.h"
#include <cstring>
#include <elf.h>
#include <iomanip>
#include <link.h>
#include <sstream>
#include <sys/system_properties.h>
#include <new>

namespace artdcevm::android13 {
namespace {
Android13Backend* routed_backend = nullptr;
jvmtiError RedefineHook(jvmtiEnv* caller, jint count,
                        const jvmtiClassDefinition* definitions) {
  auto* backend = routed_backend;
  if (artdcevm::internal_redefinition)
    return backend->original_redefine()(caller, count, definitions);
  try {
    std::lock_guard<std::mutex> lock(artdcevm::TransactionMutex());
    return backend->RouteRedefinition(caller, count, definitions);
  } catch (const std::bad_alloc&) {
    return JVMTI_ERROR_OUT_OF_MEMORY;
  }
}
std::string Quote(const std::string& s) {
  std::string r = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') r += '\\';
    if (c >= 32) r += c;
  }
  return r + '"';
}
}

int Android13Backend::Attach(JavaVM* vm) {
  if (env_) return JNI_OK;
  char sdk[PROP_VALUE_MAX]{};
  __system_property_get("ro.build.version.sdk", sdk);
  if (std::strcmp(sdk, "33") != 0) return JNI_ERR;
  if (vm->GetEnv(reinterpret_cast<void**>(&env_), JVMTI_VERSION_1_2) != JNI_OK) return JNI_ERR;
  vm_ = vm;
  jvmtiCapabilities potential{};
  if (env_->GetPotentialCapabilities(&potential) != JVMTI_ERROR_NONE) return JNI_ERR;
  jvmtiCapabilities requested{};
  requested.can_redefine_classes = potential.can_redefine_classes;
  requested.can_retransform_classes = potential.can_retransform_classes;
  requested.can_tag_objects = potential.can_tag_objects;
  if (env_->AddCapabilities(&requested) != JVMTI_ERROR_NONE || env_->GetCapabilities(&caps_) != JVMTI_ERROR_NONE) return JNI_ERR;
  jint count = 0;
  jvmtiExtensionFunctionInfo* functions = nullptr;
  if (env_->GetExtensionFunctions(&count, &functions) != JVMTI_ERROR_NONE) return JNI_ERR;
  for (int i = 0; i < count; ++i) {
    auto& f = functions[i];
    extensions_.emplace_back(f.id);
    if (std::strcmp(f.id, "com.android.art.class.structurally_redefine_classes") == 0)
      structural_ = reinterpret_cast<StructuralFn>(f.func);
    if (std::strcmp(f.id, "com.android.art.class.is_structurally_modifiable_class") == 0)
      modifiable_ = reinterpret_cast<ModifiableFn>(f.func);
    env_->Deallocate(reinterpret_cast<unsigned char*>(f.id));
    env_->Deallocate(reinterpret_cast<unsigned char*>(f.short_description));
    for (int j = 0; j < f.param_count; ++j) env_->Deallocate(reinterpret_cast<unsigned char*>(f.params[j].name));
    env_->Deallocate(reinterpret_cast<unsigned char*>(f.params));
    env_->Deallocate(reinterpret_cast<unsigned char*>(f.errors));
  }
  env_->Deallocate(reinterpret_cast<unsigned char*>(functions));
  body_ = std::make_unique<JvmtiBodyRedefiner>(env_, caps_.can_redefine_classes != 0);
  additive_ = std::make_unique<ArtAdditiveRedefiner>(env_, structural_, modifiable_);
  __android_log_print(ANDROID_LOG_INFO, "ArtDcevm", "Android 13 attached: redefine=%d structural=%d", caps_.can_redefine_classes, structural_ != nullptr);
  return JNI_OK;
}

std::string Android13Backend::Capabilities() const {
  auto art = FindLoadedModule("libart.so");
  auto jvmti = FindLoadedModule("libopenjdkjvmti.so");
  std::ostringstream s;
  s << "{\"backend\":\"android13-arm64\",\"attached\":" << (env_ ? "true" : "false")
    << ",\"methodBody\":" << (caps_.can_redefine_classes ? "true" : "false")
    << ",\"nativeAdditive\":" << (structural_ && modifiable_ ? "true" : "false")
    << ",\"evolutionAvailable\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"standardRedefineRouted\":" << (redefine_hook_ ? "true" : "false")
    << ",\"nativeDelete\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"fieldTypeEvolution\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"methodSignatureEvolution\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"constructorDeletion\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"nativeMethodDeletion\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"classInitializerDeletion\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"memberStorageChange\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"nativeImplementationChange\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"memberModifierChange\":false"
    << ",\"privateMethodTransitionNoSubclass\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"privatePublicInstanceMethodTransition\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"privateFieldVisibilityTransition\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"memberVisibilityWidening\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"memberVisibilityNarrowing\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"instanceMethodSynchronizationChange\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"javaMethodSynchronizationChange\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"methodFinalTransition\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"fieldFinalTransition\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"fieldVolatileTransition\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"classFinalTransition\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"abstractMethodTransition\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"classAbstractConservativeTransition\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"combinedAbstractTransition\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"hierarchyAddition\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"hierarchyNarrowing\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"hierarchyBatchDependencies\":"
    << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"hierarchyChange\":false,\"metadataReclamation\":false,\"fullDcevmComplete\":false"
    << ",\"evolutionError\":" << Quote(evolution_error_)
    << ",\"sourceCommitCalls\":" << ValidationHooks::SourceCommitCalls()
    << ",\"artPath\":" << Quote(art.path)
    << ",\"artBuildId\":" << Quote(art.build_id)
    << ",\"jvmtiBuildId\":" << Quote(jvmti.build_id)
    << ",\"privateHooksSupported\":" << ((art.build_id == cas::kArtBuildId && jvmti.build_id == cas::kJvmtiBuildId) ? "true" : "false")
    << ",\"validationHooksInstalled\":" << (validation_hooks_.installed() ? "true" : "false")
    << ",\"hookFieldCalls\":" << ValidationHooks::FieldCalls()
    << ",\"hookMethodCalls\":" << ValidationHooks::MethodCalls()
    << ",\"hookMappingCalls\":" << ValidationHooks::MappingCalls()
    << ",\"hookError\":" << Quote(hook_error_) << ",\"extensions\":[";
  for (size_t i = 0; i < extensions_.size(); ++i) { if (i) s << ','; s << Quote(extensions_[i]); }
  s << "]}";
  return s.str();
}

int Android13Backend::Apply(Operation op, const std::vector<Definition>& defs) {
  if (!env_) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  ClassRedefiner* engine = nullptr;
  switch (op) {
    case Operation::kBody: engine = body_.get(); break;
    case Operation::kStructure: engine = additive_.get(); break;
    case Operation::kEvolve: engine = evolution_.get(); break;
    default: return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  }
  return engine && engine->Available() ? engine->Apply(defs) : JVMTI_ERROR_NOT_AVAILABLE;
}

std::string Android13Backend::ErrorName(int error) const {
  char* name = nullptr;
  if (env_ && env_->GetErrorName(static_cast<jvmtiError>(error), &name) == JVMTI_ERROR_NONE) {
    std::string result(name);
    env_->Deallocate(reinterpret_cast<unsigned char*>(name));
    return result;
  }
  return "ENGINE_ERROR_" + std::to_string(error);
}
int Android13Backend::EnableValidationHooks() {
  if (!env_) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  return validation_hooks_.Install(hook_engine_, &hook_error_) ? JVMTI_ERROR_NONE : JVMTI_ERROR_NOT_AVAILABLE;
}
int Android13Backend::InitializeEvolution(JNIEnv* jni, const char* path, jobject calibration) {
  if (!env_ || !vm_) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  if (!caps_.can_redefine_classes) return JVMTI_ERROR_MUST_POSSESS_CAPABILITY;
  if (redefine_hook_) return JVMTI_ERROR_NONE;
  if (routing_attempted_) return JVMTI_ERROR_NOT_AVAILABLE;
  evolution_error_ = InitializeSourceEngine(jni, path, calibration);
  if (!evolution_error_.empty()) return JVMTI_ERROR_NOT_AVAILABLE;
  evolution_ = std::make_unique<SourceClassRedefiner>(vm_, env_);
  routing_attempted_ = true;
  void* target = hook_engine_.FindSymbol("libopenjdkjvmti.so", cas::kRedefineClasses);
  if (!target) {
    evolution_error_ = "Missing named RedefineClasses symbol; restart the process";
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  routed_backend = this;
  redefine_hook_ = hook_engine_.Install(target, reinterpret_cast<void*>(RedefineHook),
                                        reinterpret_cast<void**>(&original_redefine_), &evolution_error_);
  if (!redefine_hook_) {
    routed_backend = nullptr;
    evolution_error_ += "; restart the process";
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  return JVMTI_ERROR_NONE;
}
jvmtiError Android13Backend::RouteRedefinition(jvmtiEnv* caller, jint count,
                                                const jvmtiClassDefinition* definitions) {
  // Preserve ART's argument and zero-count semantics before inspecting any class.
  if (!caller || count <= 0 || !definitions)
    return original_redefine_(caller, count, definitions);
  jvmtiCapabilities caller_caps{};
  jvmtiError result = caller->GetCapabilities(&caller_caps);
  if (result != JVMTI_ERROR_NONE) return result;
  if (!caller_caps.can_redefine_classes) return JVMTI_ERROR_MUST_POSSESS_CAPABILITY;
  JNIEnv* jni = nullptr;
  if (vm_->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) != JNI_OK)
    return JVMTI_ERROR_UNATTACHED_THREAD;
  std::vector<Definition> batch;
  batch.reserve(count);
  for (jint i = 0; i < count; ++i) {
    const auto& definition = definitions[i];
    if (!definition.klass || definition.class_byte_count <= 0 || !definition.class_bytes)
      return original_redefine_(caller, count, definitions);
    jboolean modifiable = JNI_FALSE;
    result = caller->IsModifiableClass(definition.klass, &modifiable);
    if (result != JVMTI_ERROR_NONE) return result;
    if (!modifiable) return JVMTI_ERROR_UNMODIFIABLE_CLASS;
    batch.push_back({definition.klass, std::vector<unsigned char>(definition.class_bytes,
                   definition.class_bytes + definition.class_byte_count)});
  }
  BatchPlan plan;
  std::string error;
  result = static_cast<jvmtiError>(PlanEvolution(caller, jni, batch, &plan, &error));
  if (result != JVMTI_ERROR_NONE) return result;
  if (plan.required_features == 0)
    return original_redefine_(caller, count, definitions);
  constexpr uint32_t additive = evolution::kAddField | evolution::kAddMethod;
  if ((plan.required_features & ~additive) == 0)
    return structural_ ? structural_(caller, count, definitions) : JVMTI_ERROR_NOT_AVAILABLE;
  return static_cast<jvmtiError>(RedefineWithSource(caller, vm_, count, definitions));
}
std::string Android13Backend::Analyze(JNIEnv* jni, const std::vector<Definition>& defs) {
  if (!env_) return "{\"code\":116,\"detail\":\"Agent is not attached\"}";
  BatchPlan plan;
  std::string error;
  int code = PlanEvolution(env_, jni, defs, &plan, &error);
  std::string result = "{\"code\":" + std::to_string(code) + ",\"detail\":" + QuoteJsonMutf8(error);
  if (!code) result += ",\"plan\":" + DescribeBatchPlan(plan);
  return result + '}';
}
}
