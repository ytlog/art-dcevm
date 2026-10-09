#pragma once
#include <cstdint>
#include <cstddef>
namespace artdcevm::abi::arm32::android17 {
// Fixed AOSP runtime/mirror/class.h declaration. Native table links are
// source uint64_t on ARM32 too; compressed HeapReferences remain uint32_t.
// Calibrate independently inside a real rooted Runnable JNI callback.
#pragma pack(push, 4)
struct Class17 {
  uint32_t klass,monitor;
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

static_assert(sizeof(artdcevm::abi::arm32::android17::Class17)==108);
