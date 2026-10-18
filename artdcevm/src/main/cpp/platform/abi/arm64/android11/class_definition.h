#pragma once
#include "platform/abi/arm64/common/class_definition.h"

// android-11.0.0_r1 openjdkjvmti/ti_class_definition.h and
// libartbase/base/mem_map.h, arm64 libc++. Data members are also identical in
// android-12.0.0_r1 and android-12.1.0_r1. Static MemMap fields add no storage.
namespace artdcevm::abi::arm64::android11 {
using class_definition::LibcppString;
using class_definition::LibcppBytes;
using class_definition::ByteRef;
struct DefinitionMemMap {
  LibcppString name;
  unsigned char* begin;
  size_t size;
  void* base_begin;
  size_t base_size;
  int protection;
  bool reuse, already_unmapped;
  size_t redzone_size;
};
static_assert(sizeof(DefinitionMemMap) == 72);
static_assert(offsetof(DefinitionMemMap, begin) == 24 && offsetof(DefinitionMemMap, protection) == 56);
struct ArtClassDefinition {
  jclass klass;
  jobject loader;
  LibcppString name;
  jobject protection_domain;
  DefinitionMemMap dex_data_mmap, temp_mmap;
  LibcppBytes dex_data_memory;
  const void* initial_dex_file_unquickened;
  ByteRef dex_data;
  LibcppBytes current_dex_memory;
  ByteRef current_dex_file;
  bool redefined, from_class_ext, initialized, structural_transform_update;
};
static_assert(offsetof(ArtClassDefinition, dex_data) == 224);
static_assert(offsetof(ArtClassDefinition, current_dex_file) == 264);
static_assert(offsetof(ArtClassDefinition, initialized) == 282);
static_assert(sizeof(ArtClassDefinition) == 288);
using ClassDefinitions = class_definition::Vector<ArtClassDefinition>;
inline class_definition::View ReadClassDefinition(const void* address) {
  const auto& value = *static_cast<const ArtClassDefinition*>(address);
  auto view = class_definition::Read(value);
  view.from_class_ext = *reinterpret_cast<const uint8_t*>(&value.from_class_ext);
  const auto& map = value.dex_data_mmap;
  // MemMap::IsValid() tests base_size_, not begin_. PROT_NONE is zero.
  if (map.base_size && map.protection == 0) {
    view.lazy = true;
    view.lazy_begin = map.begin;
    view.lazy_size = map.size;
    view.can_initialize = map.begin && map.size >= 0x70 && value.initial_dex_file_unquickened &&
        value.temp_mmap.base_size && value.temp_mmap.begin &&
        value.temp_mmap.protection == 3 && value.temp_mmap.size == map.size;
  }
  return view;
}
inline constexpr class_definition::Layout kClassDefinitions{
    sizeof(ArtClassDefinition), ReadClassDefinition, true};
}  // namespace artdcevm::abi::arm64::android11
