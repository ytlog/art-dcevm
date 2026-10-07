#include "platform/android13/member_decode.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/retired_method_registry_bridge.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/android13/retired_field_registry_abi.h"
#include "platform/android13/retired_member_guard.h"
#include "platform/android13/validation_metadata_abi.h"
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

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
  const uint32_t index = Read<uint32_t>(reflected_field, offsetof(ReflectedField, art_field_index));
  if ((index & kRetiredFieldTag) != 0) {
    auto* field = ResolveRetiredFieldToken(index);
    Require(field != nullptr);
    return field;
  }
  auto* klass = ReadHeapReference(reflected_field, offsetof(ReflectedField, declaring_class));
  const uint32_t access = Read<uint32_t>(reflected_field, offsetof(ReflectedField, access_flags));
  auto* fields = reinterpret_cast<void*>(Read<uint64_t>(klass, (access & kStatic) != 0
      ? offsetof(validation_abi::ClassPrefix, sfields)
      : offsetof(validation_abi::ClassPrefix, ifields)));
  Require(fields != nullptr && index < Read<uint32_t>(fields, 0));
  return static_cast<unsigned char*>(fields) + field_copy_abi::kFieldArrayDataOffset +
      size_t{index} * sizeof(field_copy_abi::ArtField);
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
  auto* method = reinterpret_cast<void*>(Read<uint64_t>(mirror,
      offsetof(member_decode_abi::Executable, art_method)));
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

bool ConfigureMemberDecode(HookEngine& hooks, void* implementation, JNIEnv* jni,
                           jobject calibration, std::string* error) {
  using Probe = int (*)(JNIEnv*, jobject, void* (*)(void*), FromReflectedMethod);
  auto probe = reinterpret_cast<Probe>(dlsym(implementation, "ArtDcevmSourceProbeMemberDecode"));
  auto decode = reinterpret_cast<FromReflectedMethod>(
      hooks.FindSymbol("libart.so", member_decode_abi::kFromReflectedMethod));
  if (!probe || !decode) {
    *error = "Missing member-decode calibration or named ART reflected-method decoder";
    return false;
  }
  from_reflected_method = decode;
  const int result = probe(jni, calibration, DecodeReflectedField, DecodeRunnableExecutable);
  if (result || jni->ExceptionCheck()) {
    *error = "Fixed reflected-member declarations failed calibration at step " + std::to_string(result);
    return false;
  }
  return true;
}
}
