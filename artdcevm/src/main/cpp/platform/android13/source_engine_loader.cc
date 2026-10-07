#include "platform/android13/source_engine_loader.h"
#include <dlfcn.h>
#include <limits>
#include <cstring>
#include "platform/android13/owned_calibration.h"
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
#include "platform/android13/device_compiler_diagnostics.h"
#include "platform/android13/compiled_frame_diagnostics.h"
#include "platform/android13/device_profile.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/hierarchy_validation.h"
#include "platform/android13/structural_method_updates.h"

namespace artdcevm::android13 {
namespace {
void* source_library = nullptr;
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
  if(!CalibrateOwnedMembers(device_profile,hooks,env,ti,calibration,services,readers,error))return false;
  if(env->PushLocalFrame(16)<0){*error="Owned calibration local frame allocation failed";return false;}
  auto type=env->FindClass("com/github/ytlog/artdcevm/platform/android13/CalibrationFixture");
  auto constructor=type?env->GetMethodID(type,"<init>","()V"):nullptr;
  auto fixture=constructor?env->NewObject(type,constructor):nullptr;
  bool passed=false;
  if(!fixture||env->ExceptionCheck()) *error="Owned calibration fixture is unavailable";
  else passed=CalibrateOwnedFieldStorage(device_profile,env,ti,fixture,readers,error) &&
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
std::string InitializeSourceEngine(JNIEnv* jni, const char* path, jobject calibration) {
  if (ready) return {};
  if (load_attempted) return "Source initialization previously failed; restart the process";
  load_attempted = true;
  if (!path || !calibration) return "Source library path and ABI calibration are required";
  ShadowHookEngine symbols;
  std::string error;
  if (!symbols.Initialize(&error)) return error;
  // Identity is an additional restriction. Every source declaration and owned
  // behavioral calibration below still runs before enabling private capability.
  if (!MatchDeviceProfile(&device_profile, &error)) return error;
  if (!ConfigureTransactionMetadataReader(symbols, device_profile, &error)) return error;
  if (!symbols::CheckSourceSymbols(symbols, &error)) return error;
  using Loader = void* (*)(const char*, int, const void*);
  auto loader = reinterpret_cast<Loader>(dlsym(RTLD_DEFAULT, "__loader_dlopen"));
  if (!loader) loader = reinterpret_cast<Loader>(symbols.FindSymbol("linker64", "__loader_dlopen"));
  void* caller = symbols.FindSymbol("libopenjdkjvmti.so", cas::kCheckFields);
  if (!loader || !caller) return "Required named linker/ART symbol is unavailable";
  void* library = loader(path, RTLD_NOW | RTLD_LOCAL, caller);
  if (!library) {
    const char* detail = dlerror();
    return detail ? detail : "Source engine dlopen failed";
  }
  using ConfigureReadBarrier = void (*)(void*);
  auto configure_read_barrier = reinterpret_cast<ConfigureReadBarrier>(
      dlsym(library, "ArtDcevmSourceConfigureReadBarrier"));
  void* read_barrier_mark = symbols.FindSymbol("libart.so", cas::kReadBarrierMark);
  if (!configure_read_barrier || !read_barrier_mark) {
    dlclose(library);
    return "Missing named ART read barrier or source callback";
  }
  configure_read_barrier(read_barrier_mark);
  if (!ConfigureRuntimeAccess(symbols, library, &error) ||
      !ConfigureJniMethodTargets(symbols, library, &error)) {
    dlclose(library);
    return error;
  }
  using Probe = int (*)(JNIEnv*, jobject);
  auto probe = reinterpret_cast<Probe>(dlsym(library, "ArtDcevmSourceProbe"));
  auto commit = CommitEvolutionStructurally;
  auto invalidate_jit = reinterpret_cast<ValidationHooks::InvalidateJit>(
      symbols.FindSymbol("libart.so", cas::kInvalidateAllCompiledCode));
  if (!probe || !commit ||
      !invalidate_jit) {
    dlclose(library); return "Source engine exports are incomplete";
  }
  // The source probe may touch a reflected Field, so initialize its opaque
  // registries first. No ART hook is installed until the layout probe passes.
  if (!ConfigureRetiredMethodRegistry(symbols, library, &error) ||
      !ConfigureRetiredFieldRegistry(symbols, library, &error)) {
    dlclose(library);
    return error;
  }
  // Exercise the compiled AOSP object layout against JNI-visible values before
  // installing any source hooks. Symbol names alone do not establish ABI safety.
  int code = probe(jni, calibration);
  if (code != 0 || jni->ExceptionCheck()) {
    dlclose(library); return "Source ABI probe failed at step " + std::to_string(code);
  }
  if (!ConfigureRedefinitionFailures(symbols, &error) ||
      !ConfigureEvolutionFieldCopy(symbols, library, jni, calibration, &error) ||
      !ConfigureEvolutionFieldValidation(symbols, library, &error) ||
      !ConfigureValidationMetadata(symbols, library, &error) ||
      !ConfigureLoadedClassGuards(symbols, RuntimeClassLinker(), &error) ||
      !ConfigureHierarchyValidation(symbols, library, &error) ||
      !ConfigureNativeObjectAccess(symbols, library, jni, calibration, &error) ||
      !CalibrateJniIdDecode(library, jni, calibration, &error) ||
      !ConfigureMemberDecode(symbols, library, jni, calibration, &error) ||
      !ConfigureMemberAccessGuards(symbols, library, jni, calibration, &error) ||
      !ConfigureDeviceDiagnostics(symbols, library, &error) ||
      !ConfigureEvolutionMethodValidation(symbols, library, &error) ||
      !ConfigureEvolutionClassValidation(symbols, library, &error)) {
    dlclose(library);
    return error;
  }
  if (!ConfigureThreadSuspendScopes(symbols, library, &error) ||
      !CalibrateOwnedBootstrap(symbols, jni, calibration, &error)) {
    dlclose(library);return error;
  }
  using ProbeLoadedGuards = int (*)();
  auto loaded_probe = reinterpret_cast<ProbeLoadedGuards>(
      dlsym(library, "ArtDcevmSourceProbeLoadedClassGuards"));
  if (!loaded_probe || loaded_probe() != 0) {
    dlclose(library);
    return "Loaded-class guard ABI calibration failed";
  }
  // Retain this DSO after any hook attempt, even if rollback fails.
  source_library = library;
  ValidationHooks dispatcher;
  if (!dispatcher.Install(symbols, &error)) return error;
  if (!InstallRetiredMethodHooks(symbols, library, &error)) return error;
  if (!ConfigureStructuralMethodUpdates(symbols, library, &error)) return error;
  if (!ConfigureStructuralMemberMappings(symbols, library, &error)) return error;
  if (!ConfigureStructuralReflectiveTargets(symbols, library, &error)) return error;
  if (!InstallRetiredFieldHooks(symbols, library, &error)) return error;
  if (!InstallInstanceAllocationHook(symbols, library, &error)) return error;
  if (!InstallHierarchyBatchHooks(symbols, library, &error)) return error;
  if (!InstallHierarchyResolutionHooks(symbols, library, &error)) return error;
  if (!InstallClassLoaderHooks(symbols, &error)) return error;
  if (!InstallAsyncDeoptHook(symbols, library, &error)) return error;
  if (!ConfigureSelectiveDeopt(symbols, library, &error)) return error;
  if (!ConfigureInstrumentationListener(symbols, library, jni, calibration, &error)) return error;
  if (!ConfigureStructuralCommit(symbols, library, &error)) return error;
  if (!InstallSuspendDeoptHook(symbols, library, &error)) return error;
  if (!ConfigureEvolutionTransactions(symbols, library, &error)) return error;
#ifdef ARTDCEVM_TESTING
  if (!ConfigureCompiledFrameDiagnostics(symbols, library, &error)) return error;
  if (CompilerSymbolsAvailable() &&
      !ConfigureDeviceCompilerDiagnostics(symbols, library, jni, &error)) return error;
#endif
  ValidationHooks::ConfigureSource(commit,
                                   RuntimeJitCodeCache, invalidate_jit);
  ready = true;
  return {};
}
bool SourceEngineAvailable() { return ready; }
int RedefineSource(bool allow_transform_hierarchy, jvmtiEnv* env, JavaVM* vm, jint count,
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
int RedefineWithSource(jvmtiEnv* env, JavaVM* vm, jint count,
                       const jvmtiClassDefinition* definitions) {
  return RedefineSource(false, env, vm, count, definitions);
}
int RedefineStandardWithSource(jvmtiEnv* env, JavaVM* vm, jint count,
                               const jvmtiClassDefinition* definitions) {
  return RedefineSource(true, env, vm, count, definitions);
}
int SourceClassRedefiner::Apply(const std::vector<Definition>& definitions) {
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
  return RedefineWithSource(env_, vm_, static_cast<jint>(native.size()), native.data());
}
}

#ifdef ARTDCEVM_TESTING
// Diagnostics for the in-app regression harness; primary mutation uses an independent Agent.
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
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_checkJniMode(
    JNIEnv* env, jclass, jint requested) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticCheckJniMode(env, requested);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_readBarrierCalls(JNIEnv*, jclass) {
  if (!artdcevm::android13::SourceEngineAvailable()) return 0;
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
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return static_cast<jlong>(artdcevm::android13::RetiredFieldSlotCount());
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitMethodState(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticJitMethodState(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotMethodState(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticAotMethodState(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotExperiment(
    JNIEnv* env, jclass, jobject executable, jboolean enable) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticAotExperiment(env, executable, enable == JNI_TRUE);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotFrameState(
    JNIEnv* env, jclass, jobject thread, jobject executable) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticAotFrameState(env, thread, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotSuspendDeoptProbe(
    JNIEnv* env, jclass, jobject thread, jobject executable) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticAotSuspendDeoptProbe(env, thread, executable);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotSuspendDeoptHits(
    JNIEnv*, jclass, jboolean reset) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return static_cast<jlong>(artdcevm::android13::DiagnosticAotSuspendDeoptHits(reset == JNI_TRUE));
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitActiveFrameState(
    JNIEnv* env, jclass, jobject thread, jobject caller, jobject callee) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticJitActiveFrameState(env, thread, caller, callee);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitInlineState(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticJitInlineState(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitInlineExperiment(
    JNIEnv* env, jclass, jboolean enable) {
  if (!artdcevm::android13::SourceEngineAvailable() ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  return artdcevm::android13::DiagnosticJitInlineExperiment(env, enable == JNI_TRUE);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitCodeKind(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticJitCodeKind(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitCacheDetail(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticJitCacheDetail(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_invalidateAllJit(
    JNIEnv* env, jclass) {
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return artdcevm::android13::DiagnosticInvalidateAllJit(env);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_forceOptimizedJit(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::SourceEngineAvailable() ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  return artdcevm::android13::DiagnosticForceOptimizedJit(env, executable);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitCompilerOptionsState(
    JNIEnv*, jclass) {
  if (!artdcevm::android13::SourceEngineAvailable() ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  return artdcevm::android13::DiagnosticJitCompilerOptionsState();
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_configureGraphInlineProbe(
    JNIEnv* env, jclass, jobject caller, jobject callee, jboolean enable) {
  if (!artdcevm::android13::SourceEngineAvailable() ||
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
  if (!artdcevm::android13::SourceEngineAvailable()) return -1;
  return static_cast<jlong>(artdcevm::android13::DiagnosticGraphInlineHits());
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_allMethodsDeoptimized(
    JNIEnv*, jclass) {
  return artdcevm::android13::SourceEngineAvailable() &&
      artdcevm::android13::DiagnosticAllMethodsDeoptimized();
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_fieldReadProbeMode(
    JNIEnv*, jclass, jboolean enable, jboolean force_reject) {
  if (!artdcevm::android13::source_library) return false;
  return artdcevm::android13::SetReceiverGuardMode(enable, force_reject);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_fieldReadProbeHits(
    JNIEnv*, jclass) {
  if (!artdcevm::android13::source_library) return 0;
  return static_cast<jlong>(artdcevm::android13::FieldReadProbeHits());
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_invokeGuardDiagnostics(
    JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::source_library) return 0;
  return static_cast<jlong>(artdcevm::android13::InvokeGuardDiagnostics(index));
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_receiverGuardTransactionDiagnostics(
    JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::source_library) return 0;
  return static_cast<jlong>(artdcevm::android13::EvolutionGuardTransactionDiagnostics(index));
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_compiledPrecommitDiagnostics(
    JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::source_library) return 0;
  return static_cast<jlong>(artdcevm::android13::EvolutionCompiledPrecommitDiagnostics(index));
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_hierarchyDiagnostics(JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::source_library) return 0;
  return static_cast<jlong>(artdcevm::android13::HierarchyDiagnostics(index));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_testing_ForegroundStressActivity_enableDeoptTracing(JNIEnv*, jclass) {
  if (!artdcevm::android13::source_library) return false;
  artdcevm::android13::SetDeoptTracing(true);
  return true;
}
#endif  // ARTDCEVM_TESTING
