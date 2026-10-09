#include "platform/android13/native_deletion_capabilities.h"
#include "platform/android13/android13_backend.h"
#include <android/log.h>
#include "support/elf_modules.h"
#include "support/json.h"
#include "platform/common/evolution_planner.h"
#include "platform/android13/evolution_runtime.h"
#include "platform/symbols/android13.h"
#include "hooks/shadow_hook_engine.h"
#include "core/transaction_lock.h"
#include <algorithm>
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
    if(f.id)extensions_.emplace_back(f.id);
    if(f.id&&std::strcmp(f.id,"com.android.art.class.structurally_redefine_classes")==0) structural_=reinterpret_cast<StructuralFn>(f.func);
    if(f.id&&std::strcmp(f.id,"com.android.art.class.is_structurally_modifiable_class")==0) modifiable_=reinterpret_cast<ModifiableFn>(f.func);
    env_->Deallocate(reinterpret_cast<unsigned char*>(f.id));
    env_->Deallocate(reinterpret_cast<unsigned char*>(f.short_description));
    for (int j = 0; j < f.param_count; ++j) env_->Deallocate(reinterpret_cast<unsigned char*>(f.params[j].name));
    env_->Deallocate(reinterpret_cast<unsigned char*>(f.params));
    env_->Deallocate(reinterpret_cast<unsigned char*>(f.errors));
  }
  env_->Deallocate(reinterpret_cast<unsigned char*>(functions));
  // Public extension pointers carry their declared JVMTI calling convention.
  body_ = std::make_unique<JvmtiBodyRedefiner>(env_, caps_.can_redefine_classes != 0);
  additive_ = std::make_unique<ArtAdditiveRedefiner>(env_, structural_, modifiable_);
  __android_log_print(ANDROID_LOG_INFO, "ArtDcevm", "Android 13 attached: redefine=%d structural=%d", caps_.can_redefine_classes, structural_ != nullptr);
  return JNI_OK;
}

std::string Android13Backend::Capabilities() const {
  auto art = FindLoadedModule("libart.so");
  auto jvmti = FindLoadedModule("libopenjdkjvmti.so");
  ShadowHookEngine symbol_lookup;
  std::string symbol_error;
  bool private_hooks_supported = validation_hooks_.installed() ||
      (symbol_lookup.Initialize(&symbol_error) &&
       symbols::CheckValidationSymbols(symbol_lookup, &symbol_error));
  std::ostringstream s;
  s << "{\"backend\":\"android13-arm64\",\"attached\":" << (env_ ? "true" : "false")
    << ",\"methodBody\":" << (caps_.can_redefine_classes ? "true" : "false")
    << ",\"nativeAdditive\":" << (structural_ && modifiable_ ? "true" : "false")
    << ",\"evolutionAvailable\":" << (evolution_ && evolution_->Available() ? "true" : "false")
    << ",\"standardRedefineRouted\":" << (redefine_hook_ ? "true" : "false");
  WriteNativeDeletionCapabilities(s,evolution_ && evolution_->Available());
  s
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
    << ",\"privateHooksSupported\":" << (private_hooks_supported ? "true" : "false")
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
int Android13Backend::EnableStandardRedefineRouting() {
  if (!env_) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  if (!caps_.can_redefine_classes) return JVMTI_ERROR_MUST_POSSESS_CAPABILITY;
  // Keep body-only public JVMTI usable on devices without the structural extension.
  if (!structural_ || !modifiable_) return JVMTI_ERROR_NONE;
  if (redefine_hook_) return JVMTI_ERROR_NONE;
  if (routing_attempted_) return JVMTI_ERROR_NOT_AVAILABLE;
  routing_attempted_ = true;
  if (!hook_engine_.Initialize(&evolution_error_)) return JVMTI_ERROR_NOT_AVAILABLE;
  void* target = symbols::FindSymbol(hook_engine_,
      {symbols::Library::kJvmti, cas::kRedefineClasses, symbols::HookRoute::kInline});
  if (!target) {
    evolution_error_ = "Missing named RedefineClasses symbol";
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  routed_backend = this;
  redefine_hook_ = hook_engine_.Install(target, reinterpret_cast<void*>(RedefineHook),
                                        reinterpret_cast<void**>(&original_redefine_), &evolution_error_);
  if (!redefine_hook_) {
    routed_backend = nullptr;
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  return JVMTI_ERROR_NONE;
}
int Android13Backend::InitializeEvolution(JNIEnv* jni, jobject calibration) {
  if (!env_ || !vm_) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  if (!caps_.can_redefine_classes) return JVMTI_ERROR_MUST_POSSESS_CAPABILITY;
  if (evolution_ && evolution_->Available() && redefine_hook_) return JVMTI_ERROR_NONE;
  if (!structural_ || !modifiable_) return JVMTI_ERROR_NOT_AVAILABLE;
  evolution_error_ = InitializeEvolutionRuntime(jni, calibration);
  if (!evolution_error_.empty()) return JVMTI_ERROR_NOT_AVAILABLE;
  const int route = EnableStandardRedefineRouting();
  if (route != JVMTI_ERROR_NONE) return route;
  evolution_ = std::make_unique<EvolutionClassRedefiner>(vm_, env_);
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
  for (jint i = 0; i < count; ++i) {
    const auto& definition = definitions[i];
    if (!definition.klass || definition.class_byte_count <= 0 || !definition.class_bytes)
      return original_redefine_(caller, count, definitions);
    jboolean modifiable = JNI_FALSE;
    result = evolution_ && evolution_->Available()
        ? caller->IsModifiableClass(definition.klass, &modifiable)
        : modifiable_(caller, definition.klass, &modifiable);
    if (result != JVMTI_ERROR_NONE) return result;
    if (!modifiable) return JVMTI_ERROR_UNMODIFIABLE_CLASS;
  }
  // Schema snapshots can initialize dependent classes while ART is preparing
  // a multi-class hierarchy change. ART validates the final transformed DEX
  // inside one transaction, so do not pre-plan against mutable live classes.
  if (evolution_ && evolution_->Available())
    return static_cast<jvmtiError>(RedefineStandardWithEvolution(caller, vm_, count, definitions));
  return structural_(caller, count, definitions);
}
std::string Android13Backend::Analyze(JNIEnv* jni, const std::vector<Definition>& defs) {
  if (!env_) return "{\"code\":116,\"detail\":\"Agent is not attached\"}";
  evolution_planner::BatchPlan plan;
  std::string error;
  int code = evolution_planner::PlanEvolution(env_, jni, defs, &plan, &error);
  std::string result = "{\"code\":" + std::to_string(code) + ",\"detail\":" + QuoteJsonMutf8(error);
  if (!code) result += ",\"plan\":" + evolution_planner::DescribeBatchPlan(plan);
  return result + '}';
}
}
