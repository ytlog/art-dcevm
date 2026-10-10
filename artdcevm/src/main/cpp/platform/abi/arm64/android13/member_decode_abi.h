#pragma once

#include "platform/abi/arm64/android12/reflection.h"
#include "platform/common/reflection_metadata.h"
#include <cstddef>
#include <cstdint>

namespace artdcevm::android13::member_decode_abi {
// Locked runtime/mirror/{accessible_object,field,executable}.h. Derived
// managed members reuse AccessibleObject's tail padding, as the source probe
// checks against compiler-computed private member offsets.
using ReflectedField = abi::arm64::android12::ReflectedField;
using Executable = abi::arm64::android12::Executable;
inline constexpr uint32_t kStatic = 0x8;
inline constexpr char kFromReflectedMethod[] =
    "_ZN3art9ArtMethod19FromReflectedMethodERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobject";
static_assert(sizeof(ReflectedField) == 32);
static_assert(offsetof(ReflectedField, declaring_class) == 12);
static_assert(offsetof(ReflectedField, access_flags) == 20);
static_assert(offsetof(ReflectedField, art_field_index) == 24);
static_assert(offsetof(Executable, art_method) == 24);
}
