#pragma once

#include "platform/android13/heap_reference_abi.h"
#include <cstddef>
#include <cstdint>

namespace artdcevm::android13::member_decode_abi {
// Locked runtime/mirror/{accessible_object,field,executable}.h. Derived
// managed members reuse AccessibleObject's tail padding, as the source probe
// checks against compiler-computed private member offsets.
#pragma pack(push, 4)
struct ReflectedField {
  heap_reference_abi::ObjectPrefix object;
  uint8_t accessible;
  uint8_t padding[3];
  uint32_t declaring_class;
  uint32_t type;
  uint32_t access_flags;
  uint32_t art_field_index;
  uint32_t offset;
};
struct Executable {
  heap_reference_abi::ObjectPrefix object;
  uint8_t accessible;
  uint8_t has_real_parameter_data;
  uint8_t padding[2];
  uint32_t declaring_class;
  uint32_t declaring_class_of_overridden_method;
  uint32_t parameters;
  uint64_t art_method;
  uint32_t access_flags;
  uint32_t dex_method_index;
};
#pragma pack(pop)
inline constexpr uint32_t kStatic = 0x8;
inline constexpr char kFromReflectedMethod[] =
    "_ZN3art9ArtMethod19FromReflectedMethodERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobject";
static_assert(sizeof(ReflectedField) == 32);
static_assert(offsetof(ReflectedField, declaring_class) == 12);
static_assert(offsetof(ReflectedField, access_flags) == 20);
static_assert(offsetof(ReflectedField, art_field_index) == 24);
static_assert(offsetof(Executable, art_method) == 24);
}
