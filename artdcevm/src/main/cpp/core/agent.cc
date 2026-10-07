#include "platform/backend_factory.h"
#include "jvmti.h"
#include "core/transaction_lock.h"
#include <memory>
#include <mutex>
#include <limits>
#include <new>

namespace {
std::unique_ptr<artdcevm::RuntimeBackend> backend;
}
using artdcevm::TransactionMutex;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_6; }
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char*, void*) {
  std::lock_guard<std::mutex> lock(TransactionMutex());
  if (backend) return JNI_OK;
  auto candidate = artdcevm::CreateBackend();
  if (!candidate || candidate->Attach(vm) != JNI_OK) return JNI_ERR;
  backend = std::move(candidate);
  return JNI_OK;
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
