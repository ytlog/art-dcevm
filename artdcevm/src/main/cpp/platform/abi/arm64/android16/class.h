#pragma once
#include "platform/abi/arm64/object.h"
namespace artdcevm::abi::arm64::android16 {
// Fixed runtime/mirror/class.h declaration; match the ART build before use.
#pragma pack(push, 4)
struct Class16 {
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
  uint16_t copied_methods_offset;
  uint16_t virtual_methods_offset;
};
#pragma pack(pop)
}
