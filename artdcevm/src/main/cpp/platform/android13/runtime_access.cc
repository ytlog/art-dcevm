#include "platform/android13/runtime_access.h"
#include "platform/android13/runtime_access_abi.h"
#include "platform/android13/selective_deopt_abi.h"
#include <cstring>
#include <dlfcn.h>
namespace artdcevm::android13 {
namespace {
void** runtime_instance = nullptr;
using DecodeMethod = void* (*)(void*, jmethodID);
using DecodeField = void* (*)(void*, jfieldID);
DecodeMethod decode_method = nullptr;
DecodeField decode_field = nullptr;
void* Service(size_t offset) {
  void* result;
  std::memcpy(&result, static_cast<const unsigned char*>(*runtime_instance) + offset, sizeof(result));
  return result;
}
}
void* RuntimeInstance() { return *runtime_instance; }
void* RuntimeInstrumentation() {
  return static_cast<unsigned char*>(*runtime_instance) + offsetof(selective_abi::RuntimePrefix, instrumentation);
}
void* RuntimeClassLinker() { return Service(offsetof(runtime_access_abi::RuntimePrefix, class_linker)); }
void* RuntimeJniIdManager() { return Service(offsetof(runtime_access_abi::RuntimePrefix, jni_id_manager)); }
void* RuntimeJit() { return Service(offsetof(runtime_access_abi::RuntimePrefix, jit)); }
void* RuntimeJitCodeCache() {
  return Service(offsetof(runtime_access_abi::RuntimePrefix, jit)) == nullptr ? nullptr :
      Service(offsetof(runtime_access_abi::RuntimePrefix, jit_code_cache));
}
void* DecodeMethodId(jmethodID id) {
  return (reinterpret_cast<uintptr_t>(id) & 1) ? decode_method(RuntimeJniIdManager(), id) :
      reinterpret_cast<void*>(id);
}
void* DecodeFieldId(jfieldID id) {
  return (reinterpret_cast<uintptr_t>(id) & 1) ? decode_field(RuntimeJniIdManager(), id) :
      reinterpret_cast<void*>(id);
}
bool CalibrateJniIdDecode(void* implementation, JNIEnv* env, jobject calibration, std::string* error) {
  using Probe = int (*)(JNIEnv*, jobject, void* (*)(jmethodID), void* (*)(jfieldID));
  auto probe = reinterpret_cast<Probe>(dlsym(implementation, "ArtDcevmSourceProbeJniIdDecode"));
  if (!probe) { *error = "Missing JNI ID decoder calibration"; return false; }
  const int result = probe(env, calibration, DecodeMethodId, DecodeFieldId);
  if (result || env->ExceptionCheck()) {
    *error = "Named JNI ID decode failed calibration at step " + std::to_string(result);
    return false;
  }
  return true;
}
bool ConfigureRuntimeAccess(HookEngine& hooks, void* implementation, std::string* error) {
  using Probe = bool (*)(void*, const runtime_access_abi::Services*);
  auto probe = reinterpret_cast<Probe>(dlsym(implementation, "ArtDcevmSourceProbeRuntimeAccess"));
  auto instance = reinterpret_cast<void**>(hooks.FindSymbol("libart.so", runtime_access_abi::kRuntimeInstance));
  auto method = reinterpret_cast<DecodeMethod>(hooks.FindSymbol("libart.so", runtime_access_abi::kDecodeMethodId));
  auto field = reinterpret_cast<DecodeField>(hooks.FindSymbol("libart.so", runtime_access_abi::kDecodeFieldId));
  if (!probe || !instance || !*instance || !method || !field) {
    *error = "Missing named Runtime singleton, JNI ID decoders or declaration calibration";
    return false;
  }
  runtime_instance = instance;
  runtime_access_abi::Services services{
      RuntimeClassLinker(), RuntimeJniIdManager(),
      Service(offsetof(runtime_access_abi::RuntimePrefix, jit)), RuntimeJitCodeCache()};
  if (!services.class_linker || !services.jni_id_manager || !probe(*instance, &services)) {
    *error = "Fixed Runtime services failed source declaration calibration";
    return false;
  }
  decode_method = method;
  decode_field = field;
  return true;
}
}
