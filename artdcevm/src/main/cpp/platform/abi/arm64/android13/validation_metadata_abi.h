#pragma once

#include "evolution/class_schema.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/common/class_metadata_abi.h"
#include <cstdint>

namespace artdcevm::android13 {
struct ValidationMetadata {
  evolution::MappedDexMetadata dex;
  uint32_t access_flags;
};

namespace validation_abi {
// android-13.0.0_r1 runtime/mirror/{object,class,dex_cache}.h. MANAGED
// objects are packed to four-byte alignment; references have 32-bit encoding.
using ObjectPrefix = class_metadata::ObjectPrefix;
using ClassPrefix = class_metadata::Class11;
#pragma pack(push, 4)
struct DexCachePrefix {
  ObjectPrefix object;
  uint32_t class_loader;
  uint32_t location;
  uint64_t dex_file;
};
#pragma pack(pop)
// The fixed API 33 declaration retains its historical public type name.
using DexMappingPrefix = dex_mapping_abi::Prefix;
static_assert(alignof(ClassPrefix) == 4);
static_assert(offsetof(ClassPrefix, dex_cache) == 16);
static_assert(offsetof(ClassPrefix, access_flags) == 64);
static_assert(offsetof(ClassPrefix, dex_class_def_index) == 80);
static_assert(offsetof(ClassPrefix, dex_type_index) == 84);
static_assert(offsetof(ClassPrefix, status) == 112);
static_assert(offsetof(ClassPrefix, virtual_methods_offset) == 118);
static_assert(offsetof(DexCachePrefix, dex_file) == 16);
static_assert(sizeof(DexMappingPrefix) == 40);
}
}
