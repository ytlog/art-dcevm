#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
namespace artdcevm::class_metadata {
// Fixed release mirror/class.h declarations, arm64 compressed references.
// The provider must match/calibrate the device declaration, keep the class
// rooted and hold the ART mutator lock. This view never decodes references.
#pragma pack(push, 4)
struct ObjectPrefix {
  uint32_t klass;
  uint32_t monitor;
};
struct Class11 {
  ObjectPrefix object;
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
  uint16_t copied_methods_offset;
  uint16_t virtual_methods_offset;
};
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
static_assert(sizeof(Class11)==120 && offsetof(Class11,access_flags)==64 && offsetof(Class11,dex_class_def_index)==80);
static_assert(sizeof(Class16)==112 && offsetof(Class16,access_flags)==56 && offsetof(Class16,dex_class_def_index)==72);
static_assert(sizeof(Class17)==108 && offsetof(Class17,access_flags)==56 && offsetof(Class17,dex_class_def_index)==72);
struct Layout {size_t size,access_flags,dex_class_def_index,dex_type_index;};
inline constexpr Layout kLegacy{sizeof(Class11),offsetof(Class11,access_flags),offsetof(Class11,dex_class_def_index),offsetof(Class11,dex_type_index)};
inline constexpr Layout k16{sizeof(Class16),offsetof(Class16,access_flags),offsetof(Class16,dex_class_def_index),offsetof(Class16,dex_type_index)};
inline constexpr Layout k17{sizeof(Class17),offsetof(Class17,access_flags),offsetof(Class17,dex_class_def_index),offsetof(Class17,dex_type_index)};
inline const Layout* Declaration(int api) {
  if(api>=30 && api<=35)return &kLegacy;
  if(api==36)return &k16;
  return api==37?&k17:nullptr;
}
struct Identity {uint32_t dex_cache,access_flags;int32_t class_def_index,dex_type_index;};
inline bool ReadIdentity(int api,const void* klass,size_t extent,Identity* output) {
  const auto* layout=Declaration(api);
  const auto address=reinterpret_cast<uintptr_t>(klass);
  if(!layout || !klass || !output || extent<layout->size || address%4 ||
      address>std::numeric_limits<uintptr_t>::max()-layout->size)return false;
  const auto* bytes=static_cast<const unsigned char*>(klass);
  Identity result{};
  std::memcpy(&result.dex_cache,bytes+offsetof(Class11,dex_cache),4);
  result.access_flags=__atomic_load_n(reinterpret_cast<const uint32_t*>(bytes+layout->access_flags),__ATOMIC_RELAXED);
  std::memcpy(&result.class_def_index,bytes+layout->dex_class_def_index,4);
  std::memcpy(&result.dex_type_index,bytes+layout->dex_type_index,4);
  *output=result;return true;
}
}
