#include "platform/android13/runtime_access.h"
#include "platform/android13/runtime_access_abi.h"
#include "platform/android13/selective_deopt_abi.h"
#include "platform/common/runtime_services.h"
#include <cstdlib>
namespace artdcevm::android13 {
namespace {
void** runtime_instance = nullptr;
void* calibrated_instrumentation=nullptr;
void* calibrated_runtime=nullptr;
RuntimeReceivers calibrated_receivers;
OwnedJitReceivers calibrated_jit;
using DecodeMethod = void* (*)(void*, jmethodID);
using DecodeField = void* (*)(void*, jfieldID);
DecodeMethod decode_method = nullptr;
DecodeField decode_field = nullptr;
runtime_services::View Services(void* runtime) {
  runtime_services::View result;
  if(!runtime_services::Read(runtime_services::Declaration::kAndroid13,runtime,
      sizeof(runtime_access_abi::RuntimePrefix),&result)) std::abort();
  // These are original Runtime-lifetime services. Reading a replacement is not
  // calibration: never delegate a private function to an unobserved receiver.
  if(result.class_linker!=calibrated_receivers.class_linker ||
     result.jni_id_manager!=calibrated_receivers.jni_id_manager ||
     result.java_vm!=calibrated_receivers.java_vm ||
     result.thread_list!=calibrated_receivers.thread_list ||
     result.heap!=calibrated_jit.heap ||
     (result.jit && (result.jit!=calibrated_jit.jit || result.jit_code_cache!=calibrated_jit.code_cache)))
    std::abort();
  return result;
}
}
void* RuntimeInstance() {
  if(!runtime_instance || *runtime_instance!=calibrated_runtime)std::abort();
  return calibrated_runtime;
}
void* RuntimeInstrumentation() {
  (void)RuntimeInstance();
  return calibrated_instrumentation;
}
void* RuntimeClassLinker() { return Services(RuntimeInstance()).class_linker; }
void* RuntimeJniIdManager() { return Services(RuntimeInstance()).jni_id_manager; }
void* RuntimeJit() { return Services(RuntimeInstance()).jit; }
void* RuntimeJitCodeCache() {
  return Services(RuntimeInstance()).jit_code_cache;
}
void* DecodeMethodId(jmethodID id) {
  return (reinterpret_cast<uintptr_t>(id) & 1) ? decode_method(RuntimeJniIdManager(), id) :
      reinterpret_cast<void*>(id);
}
void* DecodeFieldId(jfieldID id) {
  return (reinterpret_cast<uintptr_t>(id) & 1) ? decode_field(RuntimeJniIdManager(), id) :
      reinterpret_cast<void*>(id);
}
bool ConfigureRuntimeAccess(HookEngine& hooks, const DeviceProfile& profile, std::string* error,
                            const RuntimeReceivers* receivers,const OwnedJitReceivers* jit_receivers) {
  if (!profile.Matched()) { *error = "Runtime access requires a matched device profile"; return false; }
  if(!receivers || !receivers->class_linker || !receivers->jni_id_manager || !receivers->java_vm || !receivers->thread_list || !receivers->instrumentation) {
    *error="Runtime access requires independently observed public JNI service receivers";return false;
  }
  if(!jit_receivers || !jit_receivers->jit || !jit_receivers->code_cache || !jit_receivers->heap) {
    *error="Runtime access requires independently observed JIT/cache/heap receivers";return false;
  }
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
  if(services.class_linker!=receivers->class_linker ||
     services.jni_id_manager!=receivers->jni_id_manager || services.java_vm!=receivers->java_vm ||
     services.thread_list!=receivers->thread_list) {
    *error="Runtime service storage disagrees with public JNI receiver observations";return false;
  }
  void* instrumentation=static_cast<unsigned char*>(*instance)+
      offsetof(selective_abi::RuntimePrefix,instrumentation);
  if(instrumentation!=receivers->instrumentation) {
    *error="Runtime Instrumentation storage disagrees with owned JVMTI listener receiver";return false;
  }
  if(services.jit!=jit_receivers->jit || services.jit_code_cache!=jit_receivers->code_cache ||
     services.heap!=jit_receivers->heap) {
    *error="Runtime JIT/cache/heap storage disagrees with owned ART receiver observations";return false;
  }
  calibrated_runtime=*instance;
  calibrated_receivers=*receivers;
  calibrated_jit=*jit_receivers;
  calibrated_instrumentation=instrumentation;
  runtime_instance = instance;
  decode_method = method;
  decode_field = field;
  return true;
}
}
