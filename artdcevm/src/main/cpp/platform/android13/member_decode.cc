#include "platform/android13/member_decode.h"
#include "platform/abi/arm64/android13/member_decode_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/retired_method_registry_bridge.h"
#include "platform/abi/arm64/android13/field_copy_metadata_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/common/retired_field_token.h"
#include "platform/android13/retired_member_guard.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include <cstdlib>
#include <cstring>

namespace artdcevm::android13 {
namespace {
using FromReflectedMethod = void* (*)(const void*, jobject);
FromReflectedMethod from_reflected_method = nullptr;
void Require(bool condition) { if (!condition) std::abort(); }
template<class T> T Read(const void* object, size_t offset) {
  T value;
  std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value));
  return value;
}
}

void* DecodeReflectedField(void* reflected_field) {
  using namespace member_decode_abi;
  reflection_metadata::FieldIdentity identity;
  Require(reflection_metadata::ReadField(reflection_metadata::k12,reflected_field,sizeof(ReflectedField),&identity));
  const uint32_t index = identity.array_index;
  if ((index & retirement::kRetiredFieldTag) != 0) {
    auto* field = ResolveRetiredFieldToken(index);
    Require(field != nullptr);
    return field;
  }
  auto* klass = ReadHeapReference(reflected_field, offsetof(ReflectedField, declaring_class));
  const uint32_t access = identity.access_flags;
  field_array::View fields;
  Require(field_array::ReadTrustedClass(field_array::kSplit, klass, sizeof(validation_abi::ClassPrefix),
      (access & kStatic) != 0, &fields));
  auto* result = fields.At(index);
  Require(result != nullptr && fields.Selected(index));
  return result;
}

void* DecodeRunnableExecutable(const void* access, jobject executable) {
  return from_reflected_method(access, executable);
}
bool GuardRunnableExecutable(const void* access, jobject executable) {
  return GuardRetiredMethod(DecodeRunnableExecutable(access, executable), nullptr, nullptr);
}
bool GuardNativeExecutable(JNIEnv* env, jobject executable) {
  NativeObjectAccess access(env);
  // Registered FAST_NATIVE entrypoint: ART enters it while Runnable.
  Require(access.Supported());
  auto* mirror = access.Decode(executable);
  void* method = nullptr;
  Require(reflection_metadata::ReadExecutableMethod(reflection_metadata::k12,mirror,sizeof(member_decode_abi::Executable),&method));
  return GuardRetiredMethod(method, access.Self(), nullptr);
}
bool GuardNativeReflectedField(JNIEnv* env, jobject field) {
  NativeObjectAccess access(env);
  // Registered FAST_NATIVE entrypoint: ART enters it while Runnable.
  Require(access.Supported());
  return GuardRetiredField(DecodeReflectedField(access.Decode(field)), access.Self(), nullptr);
}
bool GuardRunnableMethodId(jmethodID method) {
  if (RetiredMethodCount() == 0) return false;
  return GuardRetiredMethod(DecodeMethodId(method), nullptr, nullptr);
}
bool GuardNativeMethodId(JNIEnv* env, jmethodID method) {
  NativeObjectAccess access(env);
  Require(access.Supported());
  if (RetiredMethodCount() == 0) return false;
  return GuardRetiredMethod(DecodeMethodId(method), access.Self(), nullptr);
}
bool GuardRunnableFieldId(jfieldID field) {
  return GuardRetiredField(DecodeFieldId(field), nullptr, nullptr);
}
bool GuardNativeFieldId(JNIEnv* env, jfieldID field) {
  NativeObjectAccess access(env);
  Require(access.Supported());
  return GuardRetiredField(DecodeFieldId(field), access.Self(), nullptr);
}

bool ConfigureMemberDecode(HookEngine& hooks, const DeviceProfile& profile, std::string* error) {
  if (!profile.Matched()) { *error = "Member decoding requires a matched device profile"; return false; }
  auto decode = reinterpret_cast<FromReflectedMethod>(
      hooks.FindSymbol("libart.so", member_decode_abi::kFromReflectedMethod));
  if (!decode) {
    *error = "Missing named ART reflected-method decoder";
    return false;
  }
  from_reflected_method = decode;
  return true;
}
}
