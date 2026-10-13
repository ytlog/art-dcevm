#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "platform/abi/arm64/common/runtime_objects.h"

// mirror/class.h of android-17.0.0_r1 (b753cf97923c3695338d21466fa14c57b480a59a):
// static and instance fields share one fields_ array;
// access_flags_ follows the native member arrays.
namespace artdcevm::abi::arm64::android17 {
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
  uint64_t fields;
  uint64_t methods;
  uint32_t access_flags;
};
static_assert(offsetof(Class, fields) == 40 && offsetof(Class, methods) == 48 &&
              offsetof(Class, access_flags) == 56);

// Slots of the native arrays holding every declared field of `klass`.
inline std::array<uint64_t*, 1> FieldArraySlots(Class& klass) { return {&klass.fields}; }
}  // namespace artdcevm::abi::arm64::android17
