#include "platform/android13/runtime_access.h"
#include "platform/android13/runtime_access_abi.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include "device_profile_fixture.h"
namespace api = artdcevm::android13;
namespace abi = api::runtime_access_abi;
namespace {
abi::RuntimePrefix runtime{};
void* instance = &runtime;
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
  size_t lookups = 0;
  void** singleton = &instance;
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* library, const char* name) override {
    ++lookups;
    assert(std::strcmp(library, "libart.so") == 0);
    if (missing && std::strcmp(missing, name) == 0) return nullptr;
    if (std::strcmp(name, abi::kRuntimeInstance) == 0) return singleton;
    if (std::strcmp(name, abi::kDecodeMethodId) == 0) return reinterpret_cast<void*>(Method);
    if (std::strcmp(name, abi::kDecodeFieldId) == 0) return reinterpret_cast<void*>(Field);
    std::abort();
  }
  void* Install(void*, void*, void**, std::string*) override { std::abort(); }
  bool Remove(void*, std::string*) override { std::abort(); }
};
}
int main() {
  runtime.class_linker = reinterpret_cast<void*>(0x1000);
  runtime.jni_id_manager = reinterpret_cast<void*>(0x2000);
  runtime.jit = reinterpret_cast<void*>(0x3000);
  runtime.jit_code_cache = reinterpret_cast<void*>(0x4000);
  const auto profile = MatchedProfileForTest();
  Hooks hooks;
  std::string error;
  assert(!api::ConfigureRuntimeAccess(hooks, api::DeviceProfile{}, &error));
  assert(hooks.lookups == 0);
  for (const char* missing : {abi::kRuntimeInstance, abi::kDecodeMethodId, abi::kDecodeFieldId}) {
    hooks.missing = missing;
    assert(!api::ConfigureRuntimeAccess(hooks, profile, &error) && !error.empty());
  }
  hooks.missing = nullptr;
  instance = nullptr;
  assert(!api::ConfigureRuntimeAccess(hooks, profile, &error));
  instance = &runtime;
  assert(!api::ConfigureRuntimeAccess(hooks, api::DeviceProfile{}, &error));
  assert(api::ConfigureRuntimeAccess(hooks, profile, &error));
  assert(api::RuntimeInstance() == &runtime);
  assert(api::RuntimeJit() == runtime.jit);
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
  // A failed candidate rebind must not replace the previous singleton binding.
  abi::RuntimePrefix bad_runtime{};
  void* bad_instance=&bad_runtime;
  hooks.singleton=&bad_instance;
  assert(!api::ConfigureRuntimeAccess(hooks,profile,&error));
  assert(api::RuntimeInstance()==&runtime && api::RuntimeClassLinker()==runtime.class_linker);
  assert(api::DecodeMethodId(reinterpret_cast<jmethodID>(1))==reinterpret_cast<void*>(16));
  hooks.singleton=&instance;
  runtime.jni_id_manager = reinterpret_cast<void*>(0x5000);
  assert(api::DecodeMethodId(reinterpret_cast<jmethodID>(1)) == reinterpret_cast<void*>(16));
  runtime.jni_id_manager = nullptr;
  assert(!api::ConfigureRuntimeAccess(hooks, profile, &error));
  runtime.jni_id_manager = reinterpret_cast<void*>(0x5000);
  runtime.class_linker = nullptr;
  assert(!api::ConfigureRuntimeAccess(hooks, profile, &error));
  std::cout << "Runtime services and JNI IDs: named singleton, live receiver reads, null JIT, pointer/index delegation and profile rejection passed\n";
}
