#include "platform/backend_factory.h"
#include "jvmti.h"
#include "core/transaction_lock.h"
#if defined(ARTDCEVM_ARM64)
#include "platform/common/redefinition_gate.h"
#include "platform/common/native_final_write_listener.h"
#include "hooks/shadow_hook_engine.h"
#include <android/log.h>
#endif
#include <memory>
#include <mutex>
#include <limits>
#include <new>
#include <dlfcn.h>

namespace {
std::unique_ptr<artdcevm::RuntimeBackend> backend;
}
using artdcevm::TransactionMutex;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
#if defined(ARTDCEVM_ARM64)
  // This executes before VMDebug.attachAgent/GetEnv can initialize the plugin.
  // A native loader image is pinned even when the lifetime proof rejects it.
  static artdcevm::ShadowHookEngine hooks;
  static std::string mapping;static void* plugin_image=artdcevm::redefinition_gate::MapPluginForStartup(hooks,vm,&mapping);
  __android_log_print(ANDROID_LOG_INFO,"ArtDcevmStartup","JNI_OnLoad pluginMapping=%s mapped=%d",mapping.c_str(),plugin_image!=nullptr);
  if(plugin_image){
    std::string error;
    const bool observed=artdcevm::final_write_guard::ObserveLegacyDeoptimizationStartup(hooks,&error);
    __android_log_print(ANDROID_LOG_INFO,"ArtDcevmStartup","JNI_OnLoad legacyStartup=%d diagnostic=%s",observed,error.c_str());
    error.clear();const bool gated=artdcevm::redefinition_gate::InstallFirstLive(hooks,vm,&error);
    __android_log_print(ANDROID_LOG_INFO,"ArtDcevmStartup","JNI_OnLoad pristineGate=%d diagnostic=%s",gated,gated?artdcevm::redefinition_gate::Diagnostic():error.c_str());
  }
#else
  (void)vm;
#endif
  return JNI_VERSION_1_6;
}
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char*, void*) {
  std::lock_guard<std::mutex> lock(TransactionMutex());
  if (backend) return JNI_OK;
#if defined(ARTDCEVM_ARM64)
  // Retry diagnostics before creating our Env; an already initialized plugin
  // cannot replace the pristine proof obtained by JNI_OnLoad before attach.
  static artdcevm::ShadowHookEngine first_live_hooks;
  std::string gate_error;
  (void)artdcevm::redefinition_gate::InstallFirstLive(first_live_hooks,vm,&gate_error);
#endif
  auto candidate = artdcevm::CreateBackend();
  if (!candidate || candidate->Attach(vm) != JNI_OK) return JNI_ERR;
  backend = std::move(candidate);
  return JNI_OK;
}

extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm,char* options,void* reserved) {
#if defined(ARTDCEVM_ARM64)
  // Real ONLOAD is the only public lifecycle phase that rules out prior legal
  // RedefineClasses calls. A LIVE Agent_OnAttach never supplies this proof.
  jvmtiEnv* ti=nullptr;
  if(!vm||vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)!=JNI_OK||!ti)return JNI_ERR;
  static artdcevm::ShadowHookEngine startup_hooks;
  std::string error;
  const bool installed=artdcevm::redefinition_gate::InstallStartup(startup_hooks,vm,ti,&error);
  ti->DisposeEnvironment();
  // Preserve public/body support when startup proof or named gate is missing;
  // the separate atomic capability remains false with a precise diagnostic.
  (void)installed;
#endif
  return Agent_OnAttach(vm,options,reserved);
}

extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeCapabilities(JNIEnv* env, jclass) {
  std::lock_guard<std::mutex> lock(TransactionMutex());
  return env->NewStringUTF(backend ? backend->Capabilities().c_str() : "{\"attached\":false}");
}
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeErrorName(JNIEnv* env, jclass, jint code) {
  std::lock_guard<std::mutex> lock(TransactionMutex());
  return env->NewStringUTF(backend ? backend->ErrorName(code).c_str() : "NO_BACKEND");
}
extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeEnableStandardRedefineRouting(JNIEnv*, jclass) {
  std::lock_guard<std::mutex> lock(TransactionMutex());
  return backend ? backend->EnableStandardRedefineRouting() : JVMTI_ERROR_INVALID_ENVIRONMENT;
}
extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeInitializeEvolution(JNIEnv* env, jclass) {
  try {
    // JNI locals root the private SDK bootstrap object across all calibration calls.
    auto type = env->FindClass("com/github/ytlog/artdcevm/internal/EvolutionCalibration");
    auto constructor = type ? env->GetMethodID(type, "<init>", "()V") : nullptr;
    auto calibration = constructor ? env->NewObject(type, constructor) : nullptr;
    if (!calibration || env->ExceptionCheck()) return JVMTI_ERROR_INTERNAL;
    std::lock_guard<std::mutex> lock(TransactionMutex());
    return backend ? backend->InitializeEvolution(env, calibration) : JVMTI_ERROR_INVALID_ENVIRONMENT;
  } catch (const std::bad_alloc&) { return JVMTI_ERROR_OUT_OF_MEMORY; }
}

extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeInitializeCapabilities(JNIEnv* env,jclass) {
  try {
    auto type=env->FindClass("com/github/ytlog/artdcevm/internal/EvolutionCalibration");
    auto ctor=type?env->GetMethodID(type,"<init>","()V"):nullptr;
    auto fixture=ctor?env->NewObject(type,ctor):nullptr;
    if(!fixture||env->ExceptionCheck())return JVMTI_ERROR_INTERNAL;
    std::lock_guard<std::mutex> lock(TransactionMutex());
    return backend?backend->InitializeCapabilities(env,fixture):JVMTI_ERROR_INVALID_ENVIRONMENT;
  }catch(const std::bad_alloc&){return JVMTI_ERROR_OUT_OF_MEMORY;}
}

#ifdef ARTDCEVM_TESTING
// Debug probes share production initialization and the process transaction lock.
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_testing_NativeBridge_capabilities(JNIEnv* env, jclass type) {
  return Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeCapabilities(env, type);
}
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_testing_NativeBridge_errorName(JNIEnv* env, jclass type, jint code) {
  return Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeErrorName(env, type, code);
}
extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_testing_NativeBridge_enableValidationHooks(JNIEnv*, jclass) {
  std::lock_guard<std::mutex> lock(TransactionMutex());
  return backend ? backend->EnableValidationHooks() : JVMTI_ERROR_INVALID_ENVIRONMENT;
}
extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_testing_NativeBridge_enableStandardRedefineRouting(JNIEnv* env, jclass type) {
  return Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeEnableStandardRedefineRouting(env, type);
}
extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_testing_NativeBridge_initializeEvolution(JNIEnv* env, jclass type) {
  return Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeInitializeEvolution(env, type);
}
namespace {
int DecodeDefinitions(JNIEnv* env, jobjectArray classes, jobjectArray dexes, std::vector<artdcevm::Definition>* defs) {
  if (!classes || !dexes) return JVMTI_ERROR_NULL_POINTER;
  const jsize n = env->GetArrayLength(classes);
  if (n <= 0 || n != env->GetArrayLength(dexes)) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  if (n > std::numeric_limits<jint>::max() - 16 || env->EnsureLocalCapacity(n + 16) != JNI_OK)
    return JVMTI_ERROR_OUT_OF_MEMORY;
  for (jsize i = 0; i < n; ++i) {
    auto klass = static_cast<jclass>(env->GetObjectArrayElement(classes, i));
    auto bytes = static_cast<jbyteArray>(env->GetObjectArrayElement(dexes, i));
    if (!klass || !bytes || env->ExceptionCheck()) return JVMTI_ERROR_NULL_POINTER;
    const jsize size = env->GetArrayLength(bytes);
    if (size <= 0) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
    artdcevm::Definition def{klass, std::vector<unsigned char>(size)};
    env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(def.dex.data()));
    env->DeleteLocalRef(bytes);
    if (env->ExceptionCheck()) return JVMTI_ERROR_OUT_OF_MEMORY;
    defs->push_back(std::move(def));
  }
  // JNI releases retained class local references at the end of this native call.
  return JVMTI_ERROR_NONE;
}
}
extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_testing_NativeBridge_apply(JNIEnv* env, jclass, jint operation, jobjectArray classes, jobjectArray dexes) {
  try {
    std::vector<artdcevm::Definition> defs;
    int error = DecodeDefinitions(env, classes, dexes, &defs);
    if (error) return error;
    std::lock_guard<std::mutex> lock(TransactionMutex());
    artdcevm::InternalRedefinitionScope internal;
    return backend ? backend->Apply(static_cast<artdcevm::Operation>(operation), defs) : JVMTI_ERROR_INVALID_ENVIRONMENT;
  } catch (const std::bad_alloc&) { return JVMTI_ERROR_OUT_OF_MEMORY; }
}
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_testing_NativeBridge_analyze(JNIEnv* env, jclass, jobjectArray classes, jobjectArray dexes) {
  try {
    std::vector<artdcevm::Definition> defs;
    int error = DecodeDefinitions(env, classes, dexes, &defs);
    if (env->ExceptionCheck()) return nullptr;
    std::lock_guard<std::mutex> lock(TransactionMutex());
    if (!backend) error = JVMTI_ERROR_INVALID_ENVIRONMENT;
    std::string result = error ? "{\"code\":" + std::to_string(error) + "}" : backend->Analyze(env, defs);
    if (env->ExceptionCheck()) return nullptr;
    return env->NewStringUTF(result.c_str());
  } catch (const std::bad_alloc&) { return env->NewStringUTF("{\"code\":110}"); }
}

#endif  // ARTDCEVM_TESTING
