#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "platform/abi/arm64/common/runtime_objects.h"

// mirror/class.h of android-13.0.0_r1 (88baac34da072b41917b4adc1987c55546266b33):
// instance and static fields are in separate ifields_ and sfields_ arrays;
// access_flags_ follows the native member arrays.
namespace artdcevm::abi::arm64::android13 {
// runtime/class_status.h. kErrorResolved still satisfies Class::IsResolved;
// unpublished, discarded replacements must use kErrorUnresolved instead.
enum class ClassStatus : uint8_t { kErrorResolved = 2, kErrorUnresolved = 3, kResolved = 7 };
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
  uint32_t class_flags;
  uint32_t class_size;
  int32_t clinit_thread_id;
  int32_t dex_class_def_index;
  int32_t dex_type_index;
  uint32_t num_reference_instance_fields;
  uint32_t num_reference_static_fields;
  uint32_t object_size;
  uint32_t object_size_alloc_fast_path;
  uint32_t primitive_type;
  uint32_t reference_instance_offsets;
  uint32_t status;
};
static_assert(offsetof(Class, ifields) == 40 && offsetof(Class, methods) == 48 &&
              offsetof(Class, sfields) == 56 && offsetof(Class, access_flags) == 64);

// Slots of the native arrays holding every declared field of `klass`.
inline std::array<uint64_t*, 2> FieldArraySlots(Class& klass) {
  return {&klass.ifields, &klass.sfields};
}
}  // namespace artdcevm::abi::arm64::android13
