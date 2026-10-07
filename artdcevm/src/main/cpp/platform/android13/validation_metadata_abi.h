#pragma once

#include "evolution/class_schema.h"
#include <cstdint>

namespace artdcevm::android13 {
struct ValidationMetadata {
  evolution::MappedDexMetadata dex;
  uint32_t access_flags;
};
using ProbeValidationMetadata = bool (*)(void*, const ValidationMetadata*);

namespace validation_abi {
// android-13.0.0_r1 runtime/mirror/{object,class,dex_cache}.h. MANAGED
// objects are packed to four-byte alignment; references have 32-bit encoding.
#pragma pack(push, 4)
struct ObjectPrefix {
  uint32_t klass;
  uint32_t monitor;
};
struct ClassPrefix {
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
};
struct DexCachePrefix {
  ObjectPrefix object;
  uint32_t class_loader;
  uint32_t location;
  uint64_t dex_file;
};
#pragma pack(pop)
// libdexfile/dex/dex_file.h: vptr followed by the four immutable ranges.
struct DexMappingPrefix {
  const void* vtable;
  const unsigned char* begin;
  size_t size;
  const unsigned char* data_begin;
  size_t data_size;
};
static_assert(alignof(ClassPrefix) == 4);
static_assert(offsetof(ClassPrefix, dex_cache) == 16);
static_assert(offsetof(ClassPrefix, access_flags) == 64);
static_assert(offsetof(ClassPrefix, dex_class_def_index) == 80);
static_assert(offsetof(ClassPrefix, dex_type_index) == 84);
static_assert(offsetof(DexCachePrefix, dex_file) == 16);
static_assert(sizeof(DexMappingPrefix) == 40);
}
}
