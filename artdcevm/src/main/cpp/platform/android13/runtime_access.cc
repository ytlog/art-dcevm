#include "platform/android13/runtime_access.h"
#include "platform/android13/runtime_access_abi.h"
#include "platform/android13/selective_deopt_abi.h"
#include <cstring>
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
bool ConfigureRuntimeAccess(HookEngine& hooks, const DeviceProfile& profile, std::string* error) {
  if (!profile.Matched()) { *error = "Runtime access requires a matched device profile"; return false; }
  auto instance = reinterpret_cast<void**>(hooks.FindSymbol("libart.so", runtime_access_abi::kRuntimeInstance));
  auto method = reinterpret_cast<DecodeMethod>(hooks.FindSymbol("libart.so", runtime_access_abi::kDecodeMethodId));
  auto field = reinterpret_cast<DecodeField>(hooks.FindSymbol("libart.so", runtime_access_abi::kDecodeFieldId));
  if (!instance || !*instance || !method || !field) {
    *error = "Missing named Runtime singleton, JNI ID decoders";
    return false;
  }
  runtime_instance = instance;
  runtime_access_abi::Services services{
      RuntimeClassLinker(), RuntimeJniIdManager(),
      Service(offsetof(runtime_access_abi::RuntimePrefix, jit)), RuntimeJitCodeCache()};
  if (!services.class_linker || !services.jni_id_manager) {
    *error = "Missing fixed Runtime ClassLinker or JNI ID manager";
    return false;
  }
  decode_method = method;
  decode_field = field;
  return true;
}
}
