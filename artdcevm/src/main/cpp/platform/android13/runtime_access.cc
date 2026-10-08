#include "platform/android13/runtime_access.h"
#include "platform/android13/runtime_access_abi.h"
#include "platform/android13/selective_deopt_abi.h"
#include "platform/common/runtime_services.h"
#include <cstdlib>
namespace artdcevm::android13 {
namespace {
void** runtime_instance = nullptr;
using DecodeMethod = void* (*)(void*, jmethodID);
using DecodeField = void* (*)(void*, jfieldID);
DecodeMethod decode_method = nullptr;
DecodeField decode_field = nullptr;
runtime_services::View Services(void* runtime) {
  runtime_services::View result;
  if(!runtime_services::Read(runtime_services::Declaration::kAndroid13,runtime,
      sizeof(runtime_access_abi::RuntimePrefix),&result)) std::abort();
  return result;
}
}
void* RuntimeInstance() { return *runtime_instance; }
void* RuntimeInstrumentation() {
  return static_cast<unsigned char*>(*runtime_instance) + offsetof(selective_abi::RuntimePrefix, instrumentation);
}
void* RuntimeClassLinker() { return Services(*runtime_instance).class_linker; }
void* RuntimeJniIdManager() { return Services(*runtime_instance).jni_id_manager; }
void* RuntimeJit() { return Services(*runtime_instance).jit; }
void* RuntimeJitCodeCache() {
  return Services(*runtime_instance).jit_code_cache;
}
void* DecodeMethodId(jmethodID id) {
  return (reinterpret_cast<uintptr_t>(id) & 1) ? decode_method(RuntimeJniIdManager(), id) :
      reinterpret_cast<void*>(id);
}
void* DecodeFieldId(jfieldID id) {
  return (reinterpret_cast<uintptr_t>(id) & 1) ? decode_field(RuntimeJniIdManager(), id) :
      reinterpret_cast<void*>(id);
}
bool ConfigureRuntimeAccess(HookEngine& hooks, const DeviceProfile& profile, std::string* error) {
  if (!profile.Matched()) { *error = "Runtime access requires a matched device profile"; return false; }
  auto instance = reinterpret_cast<void**>(hooks.FindSymbol("libart.so", runtime_access_abi::kRuntimeInstance));
  auto method = reinterpret_cast<DecodeMethod>(hooks.FindSymbol("libart.so", runtime_access_abi::kDecodeMethodId));
  auto field = reinterpret_cast<DecodeField>(hooks.FindSymbol("libart.so", runtime_access_abi::kDecodeFieldId));
  if (!instance || !*instance || !method || !field) {
    *error = "Missing named Runtime singleton, JNI ID decoders";
    return false;
  }
  runtime_services::View services;
  if(!runtime_services::Read(runtime_services::Declaration::kAndroid13,*instance,
      sizeof(runtime_access_abi::RuntimePrefix),&services)) {
    *error="Invalid calibrated Runtime service storage";return false;
  }
  if (!services.class_linker || !services.jni_id_manager) {
    *error = "Missing fixed Runtime ClassLinker or JNI ID manager";
    return false;
  }
  runtime_instance = instance;
  decode_method = method;
  decode_field = field;
  return true;
}
}
