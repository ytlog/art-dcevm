#include "platform/android13/runtime_access.h"
#include "platform/android13/runtime_access_abi.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <dlfcn.h>
namespace api = artdcevm::android13;
namespace abi = api::runtime_access_abi;
namespace {
abi::RuntimePrefix runtime{};
void* instance = &runtime;
bool accept = true;
unsigned method_calls = 0, field_calls = 0;
void* Method(void* manager, jmethodID id) {
  assert(manager == runtime.jni_id_manager);
  ++method_calls;
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(id) * 16);
}
void* Field(void* manager, jfieldID id) {
  assert(manager == runtime.jni_id_manager);
  ++field_calls;
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(id) * 32);
}
class Hooks : public artdcevm::HookEngine {
 public:
  const char* missing = nullptr;
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* library, const char* name) override {
    assert(std::strcmp(library, "libart.so") == 0);
    if (missing && std::strcmp(missing, name) == 0) return nullptr;
    if (std::strcmp(name, abi::kRuntimeInstance) == 0) return &instance;
    if (std::strcmp(name, abi::kDecodeMethodId) == 0) return reinterpret_cast<void*>(Method);
    if (std::strcmp(name, abi::kDecodeFieldId) == 0) return reinterpret_cast<void*>(Field);
    std::abort();
  }
  void* Install(void*, void*, void**, std::string*) override { std::abort(); }
  bool Remove(void*, std::string*) override { std::abort(); }
};
}
extern "C" bool ArtDcevmSourceProbeRuntimeAccess(void* raw, const abi::Services* services) {
  assert(raw == instance);
  assert(services->class_linker == runtime.class_linker);
  assert(services->jni_id_manager == runtime.jni_id_manager);
  assert(services->jit == runtime.jit);
  assert(services->jit_code_cache == (runtime.jit ? runtime.jit_code_cache : nullptr));
  return accept;
}
int main() {
  runtime.class_linker = reinterpret_cast<void*>(0x1000);
  runtime.jni_id_manager = reinterpret_cast<void*>(0x2000);
  runtime.jit = reinterpret_cast<void*>(0x3000);
  runtime.jit_code_cache = reinterpret_cast<void*>(0x4000);
  void* implementation = dlopen(nullptr, RTLD_NOW);
  assert(implementation);
  Hooks hooks;
  std::string error;
  for (const char* missing : {abi::kRuntimeInstance, abi::kDecodeMethodId, abi::kDecodeFieldId}) {
    hooks.missing = missing;
    assert(!api::ConfigureRuntimeAccess(hooks, implementation, &error) && !error.empty());
  }
  hooks.missing = nullptr;
  instance = nullptr;
  assert(!api::ConfigureRuntimeAccess(hooks, implementation, &error));
  instance = &runtime;
  accept = false;
  assert(!api::ConfigureRuntimeAccess(hooks, implementation, &error));
  accept = true;
  assert(api::ConfigureRuntimeAccess(hooks, implementation, &error));
  assert(api::RuntimeClassLinker() == runtime.class_linker);
  assert(api::RuntimeJniIdManager() == runtime.jni_id_manager);
  assert(api::RuntimeJitCodeCache() == runtime.jit_code_cache);
  runtime.jit = nullptr; // Preserve the former GetJit()->GetCodeCache null guard.
  assert(api::RuntimeJitCodeCache() == nullptr);
  runtime.jit = reinterpret_cast<void*>(0x3000);
  for (uintptr_t value : {uintptr_t{0}, uintptr_t{0x8000}, uintptr_t{0x10000}}) {
    assert(api::DecodeMethodId(reinterpret_cast<jmethodID>(value)) == reinterpret_cast<void*>(value));
    assert(api::DecodeFieldId(reinterpret_cast<jfieldID>(value)) == reinterpret_cast<void*>(value));
  }
  assert(method_calls == 0 && field_calls == 0);
  for (uintptr_t value : {uintptr_t{1}, uintptr_t{3}, uintptr_t{127}}) {
    assert(api::DecodeMethodId(reinterpret_cast<jmethodID>(value)) == reinterpret_cast<void*>(value * 16));
    assert(api::DecodeFieldId(reinterpret_cast<jfieldID>(value)) == reinterpret_cast<void*>(value * 32));
  }
  assert(method_calls == 3 && field_calls == 3);
  runtime.jni_id_manager = reinterpret_cast<void*>(0x5000);
  assert(api::DecodeMethodId(reinterpret_cast<jmethodID>(1)) == reinterpret_cast<void*>(16));
  runtime.class_linker = nullptr;
  assert(!api::ConfigureRuntimeAccess(hooks, implementation, &error));
  std::cout << "Runtime services and JNI IDs: named singleton, live receiver reads, null JIT, pointer/index delegation and calibration rejection passed\n";
}
