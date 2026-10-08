#include "platform/android13/member_decode.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/native_object_access_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/retired_field_registry.h"
#include "platform/android13/validation_metadata_abi.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include "device_profile_fixture.h"
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
size_t retired_count = 0, id_method_decodes = 0, id_field_decodes = 0;
bool retired_method = false;
size_t self_offset = 8;
void* Method(const void* scope, jobject executable) {
  assert(scope == &access && executable == reinterpret_cast<jobject>(&method));
  ++method_decodes;
  return &method;
}
uint32_t thread_flags;
size_t transitions_in = 0, transitions_out = 0, native_decodes = 0;
void Enter(void* self) {
  assert(self == &thread_flags);
  assert((thread_flags >> 24) == guard::native_access_abi::kNative);
  thread_flags &= 0xffffff; ++transitions_in;
}
void Leave(void* self) {
  assert(self == &thread_flags && (thread_flags >> 24) == 0);
  thread_flags |= guard::native_access_abi::kNative << 24; ++transitions_out;
}
artdcevm::abi::arm64::android11::ReleaseObjectPointer Decode(const void* self, jobject object) {
  assert(self == &thread_flags && (thread_flags >> 24) == 0);
  ++native_decodes; return {reinterpret_cast<uintptr_t>(object)};
}
artdcevm::abi::arm64::android11::NativeOffset SelfOffset(size_t pointer_size) { assert(pointer_size == 8); return {self_offset}; }
class Hooks : public artdcevm::HookEngine {
 public:
  bool available = true;
  const char* missing = nullptr;
  size_t lookups = 0;
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* library, const char* symbol) override {
    ++lookups;
    assert(std::strcmp(library, "libart.so") == 0);
    if (!available || (missing && !std::strcmp(missing, symbol))) return nullptr;
    if (!std::strcmp(symbol, "artJniMethodEnd")) return reinterpret_cast<void*>(Enter);
    if (!std::strcmp(symbol, "artJniMethodStart")) return reinterpret_cast<void*>(Leave);
    if (!std::strcmp(symbol, guard::native_access_abi::kDecodeJObject)) return reinterpret_cast<void*>(Decode);
    if (!std::strcmp(symbol, guard::native_access_abi::kSelfOffset)) return reinterpret_cast<void*>(SelfOffset);
    assert(std::strcmp(symbol, guard::member_decode_abi::kFromReflectedMethod) == 0);
    return reinterpret_cast<void*>(Method);
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
size_t RetiredMethodCount() { return retired_count; }
void* DecodeMethodId(jmethodID id) {
  assert(id == reinterpret_cast<jmethodID>(&method) || reinterpret_cast<uintptr_t>(id) == 1);
  ++id_method_decodes; return &method;
}
void* DecodeFieldId(jfieldID id) {
  assert(id == reinterpret_cast<jfieldID>(&instance_fields.fields[0]) || reinterpret_cast<uintptr_t>(id) == 3);
  ++id_field_decodes; return &instance_fields.fields[0];
}
bool GuardRetiredField(void* member, void* thread, void* result) {
  assert(member == &instance_fields.fields[0] && (thread == &thread_flags || !thread) && !result);
  return true;
}
bool GuardRetiredMethod(void* member, void* thread, void* result) {
  assert(member == &method && (thread == nullptr || thread == &thread_flags) && result == nullptr);
  ++guard_calls;
  return retired_method;
}
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
  JNINativeInterface_ table{}; table.ExceptionCheck = NoException;
  const auto profile = MatchedProfileForTest();
  std::string error;
  assert(!guard::ConfigureMemberDecode(hooks, guard::DeviceProfile{}, &error));
  assert(!guard::ConfigureNativeObjectAccess(hooks, guard::DeviceProfile{}, &error));
  assert(hooks.lookups == 0);
  hooks.available = false;
  assert(!guard::ConfigureMemberDecode(hooks, profile, &error));
  hooks.available = true;
  assert(!guard::ConfigureMemberDecode(hooks, guard::DeviceProfile{}, &error));
  assert(guard::ConfigureMemberDecode(hooks, profile, &error));
  assert(!guard::GuardRunnableExecutable(&access, reinterpret_cast<jobject>(&method)));
  retired_method = true;
  assert(guard::GuardRunnableExecutable(&access, reinterpret_cast<jobject>(&method)));
  assert(method_decodes == 2 && guard_calls == 2);
  struct Environment { JNIEnv jni; void* self; void* vm; } environment{{&table}, &thread_flags, nullptr};
  auto executable = guard::member_decode_abi::Executable{};
  executable.art_method = reinterpret_cast<uintptr_t>(&method);
  thread_flags = (guard::native_access_abi::kNative << 24) | 0x45;
  assert(!guard::ConfigureNativeObjectAccess(hooks, guard::DeviceProfile{}, &error));
  for (const char* name : {"artJniMethodEnd", "artJniMethodStart",
       guard::native_access_abi::kDecodeJObject, guard::native_access_abi::kSelfOffset}) {
    hooks.missing = name;
    assert(!guard::ConfigureNativeObjectAccess(hooks, profile, &error));
  }
  hooks.missing = nullptr;
  self_offset = 16;
  assert(!guard::ConfigureNativeObjectAccess(hooks, profile, &error));
  self_offset = size_t{0x100000008ULL};
  assert(!guard::ConfigureNativeObjectAccess(hooks, profile, &error));
  self_offset = 8;
  assert(guard::ConfigureNativeObjectAccess(hooks, profile, &error));
  auto previous_in = transitions_in;
  {
    guard::NativeObjectAccess outer(&environment.jni);
    assert(outer.Supported() && thread_flags == 0x45);
    { guard::NativeObjectAccess inner(&environment.jni); assert(inner.Supported()); }
    assert(transitions_in == previous_in + 1 && thread_flags == 0x45);
    // Device checkpoints may change flags; scope must never restore a stale flags word.
    thread_flags |= 0x200;
  }
  assert(thread_flags == ((guard::native_access_abi::kNative << 24) | 0x245));
  assert(transitions_in == transitions_out);
  assert(guard::GuardNativeExecutable(&environment.jni, reinterpret_cast<jobject>(&executable)));
  assert(guard::GuardNativeReflectedField(&environment.jni, reinterpret_cast<jobject>(&reflected)));
  assert(transitions_in == transitions_out);
  thread_flags = 68u << 24;
  previous_in = transitions_in;
  { guard::NativeObjectAccess unsupported(&environment.jni); assert(!unsupported.Supported()); }
  assert(thread_flags == (68u << 24) && transitions_in == previous_in);
  thread_flags = 0x100;
  assert(guard::GuardNativeExecutable(&environment.jni, reinterpret_cast<jobject>(&executable)));
  assert(thread_flags == 0x100 && transitions_in == previous_in);
  assert(!guard::GuardRunnableMethodId(reinterpret_cast<jmethodID>(1)));
  assert(!guard::GuardNativeMethodId(&environment.jni, reinterpret_cast<jmethodID>(1)));
  assert(id_method_decodes == 0);
  retired_count = 1;
  for (auto id : {reinterpret_cast<jmethodID>(&method), reinterpret_cast<jmethodID>(1)}) {
    assert(guard::GuardRunnableMethodId(id));
    assert(guard::GuardNativeMethodId(&environment.jni, id));
  }
  for (auto id : {reinterpret_cast<jfieldID>(&instance_fields.fields[0]), reinterpret_cast<jfieldID>(3)}) {
    assert(guard::GuardRunnableFieldId(id));
    assert(guard::GuardNativeFieldId(&environment.jni, id));
  }
  assert(id_method_decodes == 4 && id_field_decodes == 4);
  thread_flags = guard::native_access_abi::kNative << 24;
  assert(guard::GuardNativeMethodId(&environment.jni, reinterpret_cast<jmethodID>(1)));
  assert(guard::GuardNativeFieldId(&environment.jni, reinterpret_cast<jfieldID>(3)));
  assert(thread_flags == (guard::native_access_abi::kNative << 24) && transitions_in == transitions_out);
  std::cout << "JNI ID guards: empty-registry bypass, pointer/index IDs and native/runnable delegation passed\n";
  std::cout << "native access: nested scopes, flag preservation, native/runnable guards and unsupported-state preservation passed\n";
  std::cout << "member decode: instance/static field identity, retired token bypass, named reflected method, profile rejection and guard delegation passed\n";
}
