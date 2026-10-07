#include "platform/android13/source_engine_loader.h"
#include <dlfcn.h>
#include <limits>
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
#include "platform/android13/suspend_deopt_hooks.h"
#include "platform/android13/retired_method_registry_bridge.h"
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/android13/validation_hooks.h"
#include "platform/android13/field_validation.h"
#include "platform/android13/redefinition_failure.h"
#include "platform/android13/jni_method_targets.h"
#include "platform/android13/method_validation.h"
#include "platform/android13/class_validation.h"

namespace artdcevm::android13 {
namespace {
void* source_library = nullptr;
using Redefine = jvmtiError (*)(jvmtiEnv*, jint, const jvmtiClassDefinition*);
Redefine source_redefine = nullptr;
Redefine source_standard_redefine = nullptr;
Redefine device_structural_redefine = nullptr;
jvmtiError CallDeviceStructuralRedefine(jvmtiEnv* env, jint count,
                                        const jvmtiClassDefinition* definitions) {
  return device_structural_redefine(env, count, definitions);
}
bool load_attempted = false, ready = false;
#ifdef ARTDCEVM_TESTING
void* graph_inline_hook_token = nullptr;
bool CompilerSymbolsAvailable() {
  ShadowHookEngine hooks;
  std::string error;
  return hooks.Initialize(&error) &&
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
  using ConfigureStructural = void (*)(Redefine);
  auto configure_structural = reinterpret_cast<ConfigureStructural>(
      dlsym(library, "ArtDcevmSourceConfigureStructuralRedefine"));
  device_structural_redefine = reinterpret_cast<Redefine>(
      symbols.FindSymbol("libopenjdkjvmti.so", cas::kStructuralRedefineClasses));
  if (!configure_structural || !device_structural_redefine) {
    dlclose(library);
    return "Missing named structural redefine entrypoint or source callback";
  }
  configure_structural(CallDeviceStructuralRedefine);
  if (!ConfigureJniMethodTargets(symbols, library, &error)) {
    dlclose(library);
    return error;
  }
  using Probe = int (*)(JNIEnv*, jobject);
  auto probe = reinterpret_cast<Probe>(dlsym(library, "ArtDcevmSourceProbe"));
  auto redefine = reinterpret_cast<Redefine>(dlsym(library, "ArtDcevmSourceRedefine"));
  auto standard_redefine = reinterpret_cast<Redefine>(dlsym(library, "ArtDcevmSourceRedefineStandard"));
  auto commit = reinterpret_cast<ValidationHooks::Commit>(dlsym(library, "ArtDcevmSourceCommit"));
  auto get_jit_code_cache = reinterpret_cast<ValidationHooks::GetJitCodeCache>(
      dlsym(library, "ArtDcevmSourceGetJitCodeCache"));
  auto invalidate_jit = reinterpret_cast<ValidationHooks::InvalidateJit>(
      symbols.FindSymbol("libart.so", cas::kInvalidateAllCompiledCode));
  if (!probe || !redefine || !standard_redefine || !commit || !get_jit_code_cache ||
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
      !ConfigureEvolutionFieldValidation(symbols, library, &error) ||
      !ConfigureEvolutionMethodValidation(symbols, library, &error) ||
      !ConfigureEvolutionClassValidation(symbols, library, &error)) {
    dlclose(library);
    return error;
  }
  // Retain this DSO after any hook attempt, even if rollback fails.
  source_library = library;
  ValidationHooks dispatcher;
  if (!dispatcher.Install(symbols, &error)) return error;
  if (!InstallRetiredMethodHooks(symbols, library, &error)) return error;
  if (!InstallRetiredFieldHooks(symbols, library, &error)) return error;
  if (!InstallInstanceAllocationHook(symbols, library, &error)) return error;
  if (!InstallHierarchyBatchHooks(symbols, library, &error)) return error;
  if (!InstallHierarchyResolutionHooks(symbols, library, &error)) return error;
  if (!InstallClassLoaderHooks(symbols, &error)) return error;
  if (!InstallAsyncDeoptHook(symbols, library, &error)) return error;
  if (!InstallSuspendDeoptHook(symbols, library, &error)) return error;
  ValidationHooks::ConfigureSource(commit,
                                   get_jit_code_cache, invalidate_jit);
  source_redefine = redefine;
  source_standard_redefine = standard_redefine;
  ready = true;
  return {};
}
bool SourceEngineAvailable() { return ready; }
int RedefineSource(Redefine redefine, jvmtiEnv* env, JavaVM* vm, jint count,
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
  const int result = redefine(env, count, definitions);
  for (jclass root : roots) jni->DeleteGlobalRef(root);
  return result;
}
int RedefineWithSource(jvmtiEnv* env, JavaVM* vm, jint count,
                       const jvmtiClassDefinition* definitions) {
  return RedefineSource(source_redefine, env, vm, count, definitions);
}
int RedefineStandardWithSource(jvmtiEnv* env, JavaVM* vm, jint count,
                               const jvmtiClassDefinition* definitions) {
  return RedefineSource(source_standard_redefine, env, vm, count, definitions);
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
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*, int);
  auto mode = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library, "ArtDcevmSourceCheckJniMode"));
  return mode ? mode(env, requested) : -1;
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_readBarrierCalls(JNIEnv*, jclass) {
  if (!artdcevm::android13::source_library) return 0;
  using Fn = uint64_t (*)();
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library, "ArtDcevmSourceReadBarrierCalls"));
  return fn ? static_cast<jlong>(fn()) : 0;
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
  if (!artdcevm::android13::source_library) return -1;
  using Fn = size_t (*)();
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceRetiredFieldSlotCount"));
  return fn ? static_cast<jlong>(fn()) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitMethodState(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*, jobject);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceJitMethodState"));
  return fn ? fn(env, executable) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotMethodState(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*, jobject);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceAotMethodState"));
  return fn ? fn(env, executable) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotExperiment(
    JNIEnv* env, jclass, jobject executable, jboolean enable) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*, jobject, bool);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceAotExperiment"));
  return fn ? fn(env, executable, enable == JNI_TRUE) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotFrameState(
    JNIEnv* env, jclass, jobject thread, jobject executable) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*, jobject, jobject);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceAotFrameState"));
  return fn ? fn(env, thread, executable) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotSuspendDeoptProbe(
    JNIEnv* env, jclass, jobject thread, jobject executable) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*, jobject, jobject);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceAotSuspendDeoptProbe"));
  return fn ? fn(env, thread, executable) : -1;
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_aotSuspendDeoptHits(
    JNIEnv*, jclass, jboolean reset) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = uint64_t (*)(bool);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceAotSuspendDeoptHits"));
  return fn ? static_cast<jlong>(fn(reset == JNI_TRUE)) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitActiveFrameState(
    JNIEnv* env, jclass, jobject thread, jobject caller, jobject callee) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*, jobject, jobject, jobject);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceJitActiveFrameState"));
  return fn ? fn(env, thread, caller, callee) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitInlineState(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*, jobject);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceJitInlineState"));
  return fn ? fn(env, executable) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitInlineExperiment(
    JNIEnv* env, jclass, jboolean enable) {
  if (!artdcevm::android13::source_library ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  using Fn = int (*)(JNIEnv*, bool);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceJitInlineExperiment"));
  return fn ? fn(env, enable == JNI_TRUE) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitCodeKind(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*, jobject);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceJitCodeKind"));
  return fn ? fn(env, executable) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitCacheDetail(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*, jobject);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceJitCacheDetail"));
  return fn ? fn(env, executable) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_invalidateAllJit(
    JNIEnv* env, jclass) {
  if (!artdcevm::android13::source_library) return -1;
  using Fn = int (*)(JNIEnv*);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceInvalidateAllJit"));
  return fn ? fn(env) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_forceOptimizedJit(
    JNIEnv* env, jclass, jobject executable) {
  if (!artdcevm::android13::source_library ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  using Fn = int (*)(JNIEnv*, jobject);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceForceOptimizedJit"));
  return fn ? fn(env, executable) : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_jitCompilerOptionsState(
    JNIEnv*, jclass) {
  if (!artdcevm::android13::source_library ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  using Fn = int (*)();
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceJitCompilerOptionsState"));
  return fn ? fn() : -1;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_configureGraphInlineProbe(
    JNIEnv* env, jclass, jobject caller, jobject callee, jboolean enable) {
  if (!artdcevm::android13::source_library ||
      !artdcevm::android13::CompilerSymbolsAvailable()) return -10;
  using Pause = int (*)(bool);
  using Configure = int (*)(JNIEnv*, jobject, jobject, void*);
  auto pause = reinterpret_cast<Pause>(dlsym(artdcevm::android13::source_library,
                                            "ArtDcevmSourcePauseJitForGraphHook"));
  auto configure = reinterpret_cast<Configure>(dlsym(artdcevm::android13::source_library,
                                                    "ArtDcevmSourceConfigureGraphInlineProbe"));
  void* replacement = dlsym(artdcevm::android13::source_library,
                            "ArtDcevmSourceHGraphInlineInto");
  if (!pause || !configure || !replacement) return -1;
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
  if (!artdcevm::android13::source_library) return -1;
  using Fn = uint64_t (*)();
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceGraphInlineHits"));
  return fn ? static_cast<jlong>(fn()) : -1;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_allMethodsDeoptimized(
    JNIEnv*, jclass) {
  if (!artdcevm::android13::source_library) return false;
  using Fn = bool (*)();
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceAllMethodsDeoptimized"));
  return fn && fn();
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_fieldReadProbeMode(
    JNIEnv*, jclass, jboolean enable, jboolean force_reject) {
  if (!artdcevm::android13::source_library) return false;
  using Fn = bool (*)(bool, bool);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceFieldReadProbeMode"));
  return fn && fn(enable, force_reject);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_fieldReadProbeHits(
    JNIEnv*, jclass) {
  if (!artdcevm::android13::source_library) return 0;
  using Fn = uint64_t (*)();
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceFieldReadProbeHits"));
  return fn ? static_cast<jlong>(fn()) : 0;
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_invokeGuardDiagnostics(
    JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::source_library) return 0;
  using Fn = uint64_t (*)(int);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
                                      "ArtDcevmSourceInvokeGuardDiagnostics"));
  return fn ? static_cast<jlong>(fn(index)) : 0;
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_receiverGuardTransactionDiagnostics(
    JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::source_library) return 0;
  using Fn = uint64_t (*)(int);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
      "ArtDcevmSourceReceiverGuardTransactionDiagnostics"));
  return fn ? static_cast<jlong>(fn(index)) : 0;
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_compiledPrecommitDiagnostics(
    JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::source_library) return 0;
  using Fn = uint64_t (*)(int);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library,
      "ArtDcevmSourceCompiledPrecommitDiagnostics"));
  return fn ? static_cast<jlong>(fn(index)) : 0;
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_platform_android13_SourceEngineInstrumentation_hierarchyDiagnostics(JNIEnv*, jclass, jint index) {
  if (!artdcevm::android13::source_library) return 0;
  using Fn = uint64_t (*)(int);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library, "ArtDcevmSourceHierarchyDiagnostics"));
  return fn ? static_cast<jlong>(fn(index)) : 0;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_ytlog_artdcevm_testing_ForegroundStressActivity_enableDeoptTracing(JNIEnv*, jclass) {
  if (!artdcevm::android13::source_library) return false;
  using Fn = void (*)(bool);
  auto fn = reinterpret_cast<Fn>(dlsym(artdcevm::android13::source_library, "ArtDcevmSourceSetDeoptTracing"));
  if (fn) fn(true);
  return fn != nullptr;
}
#endif  // ARTDCEVM_TESTING
