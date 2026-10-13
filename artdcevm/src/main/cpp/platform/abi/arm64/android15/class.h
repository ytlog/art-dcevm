#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "platform/abi/arm64/common/runtime_objects.h"

// mirror/class.h of android-15.0.0_r1 (bebbc3cc49f2d9d5420197df0a336fbc3fcbea40):
// instance and static fields are in separate ifields_ and sfields_ arrays;
// access_flags_ follows the native member arrays.
namespace artdcevm::abi::arm64::android15 {
struct Class {
  Object object;
  uint32_t class_loader;
  uint32_t component_type;
  uint32_t dex_cache;
  uint32_t ext_data;
  uint32_t iftable;
  uint32_t name;
  uint32_t super_class;
  uint32_t vtable;
  uint64_t ifields;
  uint64_t methods;
  uint64_t sfields;
  uint32_t access_flags;
};
static_assert(offsetof(Class, ifields) == 40 && offsetof(Class, methods) == 48 &&
              offsetof(Class, sfields) == 56 && offsetof(Class, access_flags) == 64);

// Slots of the native arrays holding every declared field of `klass`.
inline std::array<uint64_t*, 2> FieldArraySlots(Class& klass) {
  return {&klass.ifields, &klass.sfields};
}
}  // namespace artdcevm::abi::arm64::android15
