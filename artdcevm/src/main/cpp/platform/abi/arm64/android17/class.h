#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/abi/arm64/common/class_status.h"

// mirror/class.h of android-17.0.0_r1 (b753cf97923c3695338d21466fa14c57b480a59a):
// static and instance fields share one fields_ array;
// access_flags_ follows the native member arrays.
namespace artdcevm::abi::arm64::android17 {
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
  uint64_t fields;
  uint64_t methods;
  uint32_t access_flags;
  uint32_t class_flags;
  uint32_t class_size;
  // class-inl.h GetClinitThreadId: nonzero high bits denote a descriptor hash.
  static constexpr uint32_t kTidUnusedBitsMask = ~((uint32_t{1} << 22) - 1);
  static constexpr int32_t DecodeClinitThreadId(uint32_t raw) {
    return (raw & kTidUnusedBitsMask) ? 0 : static_cast<int32_t>(raw);
  }
  uint32_t clinit_thread_id_or_hash;
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
static_assert(offsetof(Class, fields) == 40 && offsetof(Class, methods) == 48 &&
              offsetof(Class, access_flags) == 56);

static_assert(offsetof(Class, status) == 104);

// Class.java names used to probe the fixed native slots through ART.
inline constexpr std::array<const char*, 1> kFieldArrayNames{"fields"};

inline const char* FieldArrayName(const Class&, size_t slot) { return kFieldArrayNames[slot]; }

// Slots of the native arrays holding every declared field of `klass`.
inline std::array<uint64_t*, 1> FieldArraySlots(Class& klass) { return {&klass.fields}; }
}  // namespace artdcevm::abi::arm64::android17
