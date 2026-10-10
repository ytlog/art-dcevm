#pragma once
#include "platform/abi/arm64/object.h"
namespace artdcevm::abi::arm64::android17 {
// Fixed runtime/mirror/class.h declaration; match the ART build before use.
// Class::kTidExtraBits/kTidUnusedBitsMask distinguish Linux tid from the
// complemented descriptor hash; this declaration alone grants no capability.
inline constexpr uint32_t kClinitTidExtraBits=10;
inline constexpr uint32_t kClinitTidUnusedBitsMask=~((uint32_t{1}<<(32-kClinitTidExtraBits))-1);
#pragma pack(push, 4)
struct Class17 {
  ObjectPrefix object;
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
#pragma pack(pop)
}
