#include "platform/android13/member_decode.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/retired_field_registry.h"
#include "platform/android13/validation_metadata_abi.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>

namespace artdcevm::android13 { void* ResolveRetiredFieldToken(uint32_t token); }
namespace guard = artdcevm::android13;
namespace {
struct FieldArray { uint32_t size; guard::field_copy_abi::ArtField fields[3]; };
FieldArray instance_fields{3, {}}, static_fields{3, {}};
guard::validation_abi::ClassPrefix klass{};
guard::member_decode_abi::ReflectedField reflected{};
guard::RetiredFieldRegistry registry;
int retired_field, method, access;
size_t heap_reads = 0, method_decodes = 0, guard_calls = 0;
bool retired_method = false, probe_rejected = false;
void* Method(const void* scope, jobject executable) {
  assert(scope == &access && executable == reinterpret_cast<jobject>(&method));
  ++method_decodes;
  return &method;
}
class Hooks : public artdcevm::HookEngine {
 public:
  bool available = true;
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* library, const char* symbol) override {
    assert(std::strcmp(library, "libart.so") == 0);
    assert(std::strcmp(symbol, guard::member_decode_abi::kFromReflectedMethod) == 0);
    return available ? reinterpret_cast<void*>(Method) : nullptr;
  }
  void* Install(void*, void*, void**, std::string*) override { std::abort(); }
  bool Remove(void*, std::string*) override { std::abort(); }
};
jboolean JNICALL NoException(JNIEnv*) { return JNI_FALSE; }
}
namespace artdcevm::android13 {
void* ReadHeapReference(void* owner, size_t offset, bool is_volatile) {
  assert(owner == &reflected && !is_volatile &&
         offset == offsetof(member_decode_abi::ReflectedField, declaring_class));
  ++heap_reads;
  return &klass;
}
void* ResolveRetiredFieldToken(uint32_t token) { return registry.Resolve(token); }
bool GuardRetiredMethod(void* member, void* thread, void* result) {
  assert(member == &method && thread == nullptr && result == nullptr);
  ++guard_calls;
  return retired_method;
}
}
extern "C" int ArtDcevmSourceProbeMemberDecode(JNIEnv*, jobject,
    void* (*read_field)(void*), void* (*read_method)(const void*, jobject)) {
  if (probe_rejected) return 7;
  assert(read_field(&reflected) == &instance_fields.fields[0]);
  assert(read_method(&access, reinterpret_cast<jobject>(&method)) == &method);
  return 0;
}
int main() {
  static_assert(offsetof(FieldArray, fields) == guard::field_copy_abi::kFieldArrayDataOffset);
  klass.ifields = reinterpret_cast<uintptr_t>(&instance_fields);
  klass.sfields = reinterpret_cast<uintptr_t>(&static_fields);
  for (bool is_static : {false, true}) {
    reflected.access_flags = is_static ? guard::member_decode_abi::kStatic : 0;
    for (uint32_t index = 0; index < 3; ++index) {
      reflected.art_field_index = index;
      assert(guard::DecodeReflectedField(&reflected) ==
          (is_static ? &static_fields.fields[index] : &instance_fields.fields[index]));
    }
  }
  registry.Retire(&retired_field);
  reflected.art_field_index = registry.TokenFor(&retired_field);
  const auto before = heap_reads;
  assert(guard::DecodeReflectedField(&reflected) == &retired_field);
  assert(heap_reads == before);  // A retired token must not consult the old Class/field array.
  reflected.art_field_index = 0; reflected.access_flags = 0;
  Hooks hooks;
  JNINativeInterface_ table{}; table.ExceptionCheck = NoException; JNIEnv jni{&table};
  auto* implementation = dlopen(nullptr, RTLD_NOW);
  assert(implementation);
  std::string error;
  hooks.available = false;
  assert(!guard::ConfigureMemberDecode(hooks, implementation, &jni, nullptr, &error));
  hooks.available = true; probe_rejected = true;
  assert(!guard::ConfigureMemberDecode(hooks, implementation, &jni, nullptr, &error));
  assert(error.find("step 7") != std::string::npos);
  probe_rejected = false;
  assert(guard::ConfigureMemberDecode(hooks, implementation, &jni, nullptr, &error));
  assert(!guard::GuardRunnableExecutable(&access, reinterpret_cast<jobject>(&method)));
  retired_method = true;
  assert(guard::GuardRunnableExecutable(&access, reinterpret_cast<jobject>(&method)));
  assert(method_decodes == 3 && guard_calls == 2);
  std::cout << "member decode: instance/static field identity, retired token bypass, named reflected method, profile rejection and guard delegation passed\n";
}
