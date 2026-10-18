#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/abi/arm64/common/class_status.h"

// mirror/class.h of android-12.0.0_r1 (8d44c3d61d4bde412f06190cd4acc4afc2ea469c)
// and android-12.1.0_r1 (ab1f62ad90952006e0a47cabdb3e5a3bcd07124e):
// instance and static fields are in separate ifields_ and sfields_ arrays;
// access_flags_ follows the native member arrays.
namespace artdcevm::abi::arm64::android12 {
using ClassStatus = abi::arm64::ClassStatus;
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
  // copied_methods_offset_ and virtual_methods_offset_ end the object.
  uint16_t copied_methods_offset, virtual_methods_offset;
};
static_assert(offsetof(Class, ifields) == 40 && offsetof(Class, methods) == 48 &&
              offsetof(Class, sfields) == 56 && offsetof(Class, access_flags) == 64);

static_assert(offsetof(Class, status) == 112);

// sizeof(mirror::Class) (MANAGED is PACKED(4)): Class::EmbeddedVTableLengthOffset.
// The embedded IMT pointer follows at ImtPtrOffset = RoundUp(this + 4, 8).
inline constexpr size_t ObjectSize(const Class*) { return offsetof(Class, virtual_methods_offset) + sizeof(uint16_t); }
static_assert(ObjectSize(nullptr) == 120);

// Class.java names used to probe the fixed native slots through ART.
inline constexpr std::array<const char*, 2> kFieldArrayNames{"iFields", "sFields"};

inline const char* FieldArrayName(const Class&, size_t slot) { return kFieldArrayNames[slot]; }

// Slots of the native arrays holding every declared field of `klass`.
inline std::array<uint64_t*, 2> FieldArraySlots(Class& klass) {
  return {&klass.ifields, &klass.sfields};
}
}  // namespace artdcevm::abi::arm64::android12
