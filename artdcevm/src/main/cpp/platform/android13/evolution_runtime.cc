#include "platform/android13/evolution_runtime.h"
#include <limits>
#include <cstring>
#include "platform/android13/owned_calibration.h"
#include "platform/android13/reflection_bindings.h"
#include "platform/android13/allocator_membership.h"
#include "platform/android13/dex_cache_method_reader.h"
#include "platform/android13/method_access.h"
#include "platform/android13/method_invoke.h"
#include "platform/android13/method_lookup.h"
#include "platform/android13/checked_method_provider.h"
#include "platform/android13/transaction_metadata.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/field_copy_storage.h"
#include "hooks/shadow_hook_engine.h"
#include "platform/symbols/android13.h"
#include "support/elf_modules.h"
#include "platform/android13/retired_method_hooks.h"
#include "platform/android13/retired_field_hooks.h"
#include "platform/android13/hierarchy_batch_hooks.h"
#include "platform/android13/hierarchy_resolution_hooks.h"
#include "platform/android13/class_loader_hooks.h"
#include "platform/android13/instance_allocation_hooks.h"
#include "platform/android13/async_deopt_hooks.h"
#include "platform/android13/member_access_guards.h"
#include "platform/android13/instrumentation_listener.h"
#include "platform/android13/thread_suspend_scope.h"
#include "platform/android13/evolution_transaction.h"
#include "platform/android13/structural_member_mapping.h"
#include "platform/android13/structural_reflective.h"
#include "platform/android13/selective_deopt.h"
#include "platform/android13/structural_commit.h"
#include "platform/android13/hierarchy_allocation.h"
#include "platform/android13/suspend_deopt_hooks.h"
#include "platform/android13/frame_recovery.h"
#include "platform/android13/retired_method_registry_bridge.h"
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/android13/validation_hooks.h"
#include "platform/android13/field_validation.h"
#include "platform/android13/redefinition_failure.h"
#include "platform/android13/jni_method_targets.h"
#include "platform/android13/method_validation.h"
#include "platform/android13/class_validation.h"
#include "platform/android13/validation_metadata.h"
#include "platform/android13/field_copy_binding.h"
#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/member_decode.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/nterp_dispatch.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/device_diagnostics.h"
#include "platform/android13/runnable_jni_diagnostics.h"
#include "platform/android13/device_compiler_diagnostics.h"
#include "platform/android13/compiled_frame_diagnostics.h"
#include "platform/android13/device_profile.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/hierarchy_validation.h"
#include "platform/android13/structural_method_updates.h"

namespace artdcevm::android13 {
namespace {
bool load_attempted = false, ready = false;
DeviceProfile device_profile;
void* DecodeCalibrationExecutable(JNIEnv* env,jobject executable) {
  NativeObjectAccess access(env);
  if(!access.Supported()) return nullptr;
  uint64_t method=0;
  std::memcpy(&method,static_cast<unsigned char*>(access.Decode(executable))+
      offsetof(member_decode_abi::Executable,art_method),sizeof(method));
  return reinterpret_cast<void*>(method);
}
bool CalibrateOwnedBootstrap(HookEngine& hooks,JNIEnv* env,jobject calibration,std::string* error) {
  JavaVM* vm=nullptr;jvmtiEnv* ti=nullptr;
  if(env->GetJavaVM(&vm)!=JNI_OK ||
      vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)!=JNI_OK) {
    *error="Owned calibration requires an attached JVMTI environment";return false;
  }
  OwnedCalibrationReaders readers{DecodeMethodId,DecodeFieldId,DecodeReflectedField,
      DecodeCalibrationExecutable,CanonicalizeJniMethod,
      {ReadCopyClass,ReadCopyField,ReadCopyValue,WriteCopyValue,TransferCopyMonitor,CopyWriteBarrier}};
  runtime_access_abi::Services services{RuntimeClassLinker(),RuntimeJniIdManager(),RuntimeJit(),RuntimeJitCodeCache()};
  if(env->PushLocalFrame(16)<0){*error="Owned calibration local frame allocation failed";return false;}
  auto type=env->FindClass("com/github/ytlog/artdcevm/internal/CalibrationFixture");
  auto constructor=type?env->GetMethodID(type,"<init>","()V"):nullptr;
  auto fixture=constructor?env->NewObject(type,constructor):nullptr;
  bool passed=false;
  if(!fixture||env->ExceptionCheck()) *error="Owned calibration fixture is unavailable";
  else passed=CalibrateRuntimeFieldIds(hooks,env,ti,calibration,fixture,error) &&
      CalibrateOwnedMembers(device_profile,hooks,env,ti,calibration,services,readers,error) &&
      CalibrateOwnedFieldStorage(device_profile,env,ti,fixture,readers,error) &&
      CalibrateOwnedCardTable(device_profile,env,fixture,readers,error) &&
      CalibrateOwnedMonitorTransfer(device_profile,env,fixture,readers.fields,error);
  env->PopLocalFrame(nullptr);return passed;
}
#ifdef ARTDCEVM_TESTING
void* graph_inline_hook_token = nullptr;
bool CompilerSymbolsAvailable() {
  ShadowHookEngine hooks;
  std::string error;
  return MatchCompilerDeviceProfile(device_profile, &error) && hooks.Initialize(&error) &&
      hooks.FindSymbol("libart-compiler.so", cas::kHGraphInlineInto) != nullptr;
}
#endif
}
std::string InitializeEvolutionRuntime(JNIEnv* jni,jobject calibration) {
  if (ready) return {};
  if (load_attempted) return "Evolution initialization previously failed; restart the process";
  load_attempted = true;
  if (!jni || !calibration) return "JNI and ABI calibration are required";
  static ShadowHookEngine elf_symbols;
#ifdef ARTDCEVM_FORCE_REFLECTION_BINDINGS_TEST
  static ReflectionBindings symbols(elf_symbols,true);
#else
  static ReflectionBindings symbols(elf_symbols);
#endif
  std::string error;
  if (!symbols.Initialize(&error) || !MatchDeviceProfile(&device_profile, &error) ||
      !ConfigureTransactionMetadataReader(symbols, device_profile, &error)) return error;
  RuntimeReceivers observed_receivers;
  if(!CalibrateRuntimeReceivers(symbols,jni,&observed_receivers,&error))return error;
  OwnedJitReceivers observed_jit;
  if(!CalibrateOwnedJitReceivers(symbols,jni,calibration,observed_receivers.jni_id_manager,&observed_jit,&error))return error;
  if (!ConfigureRuntimeAccess(symbols, device_profile, &error,&observed_receivers,&observed_jit) ||
      !ConfigureJniMethodTargets(symbols, device_profile, &error) ||
      !ConfigureRedefinitionFailures(symbols, &error) ||
      !ConfigureEvolutionFieldCopy(symbols, device_profile, &error) ||
      !ConfigureEvolutionFieldValidation(symbols, device_profile, &error) ||
      !ConfigureValidationMetadata(symbols, &error) ||
      !ConfigureLoadedClassGuards(symbols, RuntimeClassLinker(), &error) ||
      !ConfigureHierarchyValidation(symbols, device_profile, &error) ||
      !ConfigureNativeObjectAccess(symbols, device_profile, &error) ||
      !ConfigureMemberDecode(symbols, device_profile, &error) ||
      !ConfigureMemberAccessGuards(symbols, device_profile, &error) ||
      !ConfigureDeviceDiagnostics(symbols, device_profile, &error) ||
      !ConfigureEvolutionMethodValidation(symbols, device_profile, &error) ||
      !ConfigureEvolutionClassValidation(symbols, device_profile, &error) ||
      !ConfigureThreadSuspendScopes(symbols, device_profile, &error) ||
      !ConfigureFrameRecovery(symbols, device_profile, &error) ||
      !CalibrateOwnedBootstrap(symbols, jni, calibration, &error)) return error;
  AllocatorContains membership=nullptr;
#ifdef ARTDCEVM_FORCE_ALLOCATOR_MEMBERSHIP_TEST
  constexpr bool force_arena=true;
#else
  constexpr bool force_arena=false;
#endif
  if(!ConfigureDexCacheMethodReader(symbols,device_profile,jni,calibration,&error) ||
      !ConfigureMethodAccess(symbols,device_profile,jni,calibration,&error) ||
      !ConfigureMethodInvoke(symbols,device_profile,jni,calibration,&error) ||
      !ConfigureMethodLookup(symbols,device_profile,jni,calibration,&error) ||
      !ConfigureCheckedMethodProvider(symbols,device_profile,jni,calibration,&error) ||
      !ConfigureAllocatorMembership(symbols,device_profile,jni,calibration,&membership,&error,force_arena) ||
      !ConfigureRetiredMethodRegistry(symbols,membership,&error) ||
      !ConfigureRetiredFieldRegistry(membership,&error) ||
      !symbols.Configure(jni,device_profile,&error) ||
      !symbols::CheckEvolutionSymbols(symbols,&error)) return error;
  // Every proxy is owned by this DSO. Named device functions are bound above;
  // public JNI/JVMTI calibration completes before any evolution hook attempt.
  ValidationHooks dispatcher;
  if (!dispatcher.Install(symbols, &error) ||
      !InstallRetiredMethodHooks(symbols, &error) ||
      !ConfigureStructuralMethodUpdates(symbols, &error) ||
      !ConfigureStructuralMemberMappings(symbols, &error) ||
      !ConfigureStructuralReflectiveTargets(symbols, device_profile, &error) ||
      !InstallRetiredFieldHooks(symbols, device_profile, &error) ||
      !InstallInstanceAllocationHook(symbols, device_profile, &error) ||
      !InstallHierarchyBatchHooks(symbols, device_profile, &error) ||
      !InstallHierarchyResolutionHooks(symbols, device_profile, &error) ||
      !InstallClassLoaderHooks(symbols, &error) ||
      !InstallAsyncDeoptHook(symbols, device_profile, &error) ||
      !ConfigureSelectiveDeopt(symbols, device_profile, &error) ||
      !ConfigureInstrumentationListener(symbols, device_profile, &error) ||
      !ConfigureStructuralCommit(symbols, &error) ||
      !InstallSuspendDeoptHook(symbols, &error) ||
      !ConfigureEvolutionTransactions(symbols, device_profile, &error)) return error;
#ifdef ARTDCEVM_TESTING
  if (!ConfigureCompiledFrameDiagnostics(symbols, device_profile, &error)) return error;
  if (CompilerSymbolsAvailable() &&
      !ConfigureDeviceCompilerDiagnostics(symbols, device_profile, &error)) return error;
#endif
  auto invalidate = reinterpret_cast<ValidationHooks::InvalidateJit>(
      symbols.FindSymbol("libart.so", cas::kInvalidateAllCompiledCode));
  if (!invalidate) return "Missing named ART compiled-code invalidation";
  ValidationHooks::ConfigureSource(CommitEvolutionStructurally,RuntimeJitCodeCache,invalidate);
  ready = true;
  return {};
}
bool EvolutionRuntimeAvailable() { return ready; }
int RedefineOwned(bool allow_transform_hierarchy, jvmtiEnv* env, JavaVM* vm, jint count,
                   const jvmtiClassDefinition* definitions) {
  if (!ready) return JVMTI_ERROR_NOT_AVAILABLE;
  JNIEnv* jni = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) != JNI_OK)
    return JVMTI_ERROR_UNATTACHED_THREAD;
  std::vector<jclass> roots;
  roots.reserve(count);
  for (jint i = 0; i < count; ++i) {
    auto root = static_cast<jclass>(jni->NewGlobalRef(definitions[i].klass));
    if (!root) {
      for (jclass retained : roots) jni->DeleteGlobalRef(retained);
      return JVMTI_ERROR_OUT_OF_MEMORY;
    }
    roots.push_back(root);
  }
  SourceTransactionScope transaction(env, jni);
  const int result = RedefineEvolutionTransaction(env, count, definitions, allow_transform_hierarchy);
  for (jclass root : roots) jni->DeleteGlobalRef(root);
  return result;
}
int RedefineWithEvolution(jvmtiEnv* env, JavaVM* vm, jint count,
                       const jvmtiClassDefinition* definitions) {
  return RedefineOwned(false, env, vm, count, definitions);
}
int RedefineStandardWithEvolution(jvmtiEnv* env, JavaVM* vm, jint count,
                               const jvmtiClassDefinition* definitions) {
  return RedefineOwned(true, env, vm, count, definitions);
}
int EvolutionClassRedefiner::Apply(const std::vector<Definition>& definitions) {
  if (!ready) return JVMTI_ERROR_NOT_AVAILABLE;
  if (definitions.empty() || definitions.size() > static_cast<size_t>(std::numeric_limits<jint>::max()))
    return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  JNIEnv* jni = nullptr;
  if (vm_->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) != JNI_OK)
    return JVMTI_ERROR_UNATTACHED_THREAD;
  std::vector<jvmtiClassDefinition> native;
  native.reserve(definitions.size());
  for (size_t i = 0; i < definitions.size(); ++i) {
    const auto& definition = definitions[i];
    if (!definition.klass || definition.dex.empty() ||
        definition.dex.size() > static_cast<size_t>(std::numeric_limits<jint>::max()))
      return JVMTI_ERROR_ILLEGAL_ARGUMENT;
    for (size_t j = 0; j < i; ++j)
      if (jni->IsSameObject(definition.klass, definitions[j].klass)) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
    native.push_back({definition.klass, static_cast<jint>(definition.dex.size()), definition.dex.data()});
  }
  return RedefineWithEvolution(env_, vm_, static_cast<jint>(native.size()), native.data());
}
}

#ifdef ARTDCEVM_TESTING
// Diagnostics for the in-app regression harness; primary mutation uses an independent Agent.
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_runnableJniProbe(
    JNIEnv* env,jclass,jobject carrier,jobject fixture) {
  return artdcevm::android13::DiagnosticRunnableJni(env,carrier,fixture);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_commitCalls(JNIEnv*, jclass) {
  return artdcevm::android13::ValidationHooks::SourceCommitCalls();
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_inPlaceInvalidationCalls(
    JNIEnv*, jclass) {
  return artdcevm::android13::ValidationHooks::SourceInPlaceCalls();
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_rejectedCalls(JNIEnv*, jclass) {
  return artdcevm::android13::RejectedRetiredInvocations();
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_probeCheckedHandleHook(JNIEnv* env, jclass) {
  return artdcevm::android13::ProbeCheckedHandleHook(env)?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_sourceCheckedHandleCalls(JNIEnv*, jclass) {
  return artdcevm::android13::SourceCheckedHandleDispatches();
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_checkJniMode(
    JNIEnv* env, jclass, jint requested) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticCheckJniMode(env, requested);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_readBarrierCalls(JNIEnv*, jclass) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return 0;
  return static_cast<jlong>(artdcevm::android13::DiagnosticReadBarrierCalls());
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_classLoaderDeleteCalls(JNIEnv*, jclass) {
  return artdcevm::android13::ClassLoaderDeleteCalls();
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_retiredPrunedCount(
    JNIEnv*, jclass, jint kind) {
  return artdcevm::android13::RetiredPrunedCount(kind);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_retiredFieldSlotCount(
    JNIEnv*, jclass) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return static_cast<jlong>(artdcevm::android13::RetiredFieldSlotCount());
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitMethodState(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticJitMethodState(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotMethodState(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticAotMethodState(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotExperiment(
    JNIEnv* env, jclass, jobject executable, jboolean enable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticAotExperiment(env, executable, enable == JNI_TRUE);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotFrameState(
    JNIEnv* env, jclass, jobject thread, jobject executable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticAotFrameState(env, thread, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotSuspendDeoptProbe(
    JNIEnv* env, jclass, jobject thread, jobject executable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticAotSuspendDeoptProbe(env, thread, executable);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotSuspendDeoptHits(
    JNIEnv*, jclass, jboolean reset) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return static_cast<jlong>(artdcevm::android13::DiagnosticAotSuspendDeoptHits(reset == JNI_TRUE));
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitActiveFrameState(
    JNIEnv* env, jclass, jobject thread, jobject caller, jobject callee) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticJitActiveFrameState(env, thread, caller, callee);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitInlineState(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticJitInlineState(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitInlineExperiment(
    JNIEnv* env, jclass, jboolean enable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable() ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  return artdcevm::android13::DiagnosticJitInlineExperiment(env, enable == JNI_TRUE);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitCodeKind(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticJitCodeKind(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitCacheDetail(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticJitCacheDetail(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_invalidateAllJit(
    JNIEnv* env, jclass) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return artdcevm::android13::DiagnosticInvalidateAllJit(env);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_forceOptimizedJit(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable() ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  return artdcevm::android13::DiagnosticForceOptimizedJit(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitCompilerOptionsState(
    JNIEnv*, jclass) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable() ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  return artdcevm::android13::DiagnosticJitCompilerOptionsState();
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_configureGraphInlineProbe(
    JNIEnv* env, jclass, jobject caller, jobject callee, jboolean enable) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable() ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  auto pause = artdcevm::android13::DiagnosticPauseJitForGraphHook;
  auto configure = artdcevm::android13::DiagnosticConfigureGraphInlineProbe;
  void* replacement = reinterpret_cast<void*>(artdcevm::android13::DiagnosticHGraphInlineInto);
  artdcevm::ShadowHookEngine hooks;
  std::string error;
  if (!hooks.Initialize(&error)) return -2;
  if (pause(true) != 1) return -3;
  int result = 1;
  if (enable == JNI_TRUE) {
    if (artdcevm::android13::graph_inline_hook_token != nullptr) {
      result = -4;
    } else {
      void* target = hooks.FindSymbol("libart-compiler.so",
                                      artdcevm::android13::cas::kHGraphInlineInto);
      if (target == nullptr) {
        result = -5;
      } else {
        void* original = nullptr;
        void* token = hooks.Install(target, replacement, &original, &error);
        if (token == nullptr || original == nullptr) {
          if (token != nullptr) hooks.Remove(token, &error);
          result = -6;
        } else if (configure(env, caller, callee, original) != 1) {
          hooks.Remove(token, &error);
          result = -7;
        } else {
          artdcevm::android13::graph_inline_hook_token = token;
        }
      }
    }
  } else if (artdcevm::android13::graph_inline_hook_token != nullptr) {
    if (!hooks.Remove(artdcevm::android13::graph_inline_hook_token, &error)) {
      result = -8;
    } else {
      configure(env, nullptr, nullptr, nullptr);
      artdcevm::android13::graph_inline_hook_token = nullptr;
    }
  }
  if (pause(false) != 1) return -9;
  return result;
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_graphInlineHits(
    JNIEnv*, jclass) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return -1;
  return static_cast<jlong>(artdcevm::android13::DiagnosticGraphInlineHits());
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_allMethodsDeoptimized(
    JNIEnv*, jclass) {
  return artdcevm::android13::EvolutionRuntimeAvailable() &&
      artdcevm::android13::DiagnosticAllMethodsDeoptimized();
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_fieldReadProbeMode(
    JNIEnv*, jclass, jboolean enable, jboolean force_reject) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return false;
  return artdcevm::android13::SetReceiverGuardMode(enable, force_reject);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_fieldReadProbeHits(
    JNIEnv*, jclass) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return 0;
  return static_cast<jlong>(artdcevm::android13::FieldReadProbeHits());
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_invokeGuardDiagnostics(
    JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return 0;
  return static_cast<jlong>(artdcevm::android13::InvokeGuardDiagnostics(index));
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_receiverGuardTransactionDiagnostics(
    JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return 0;
  return static_cast<jlong>(artdcevm::android13::EvolutionGuardTransactionDiagnostics(index));
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_compiledPrecommitDiagnostics(
    JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return 0;
  return static_cast<jlong>(artdcevm::android13::EvolutionCompiledPrecommitDiagnostics(index));
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_hierarchyDiagnostics(JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return 0;
  return static_cast<jlong>(artdcevm::android13::HierarchyDiagnostics(index));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_testing_ForegroundStressActivity_enableDeoptTracing(JNIEnv*, jclass) {
  if (!artdcevm::android13::EvolutionRuntimeAvailable()) return false;
  artdcevm::android13::SetDeoptTracing(true);
  return true;
}
#endif  // ARTDCEVM_TESTING
