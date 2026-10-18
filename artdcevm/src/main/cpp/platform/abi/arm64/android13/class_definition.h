#pragma once
#include "platform/abi/arm64/common/class_definition.h"

// openjdkjvmti/ti_class_definition.h: the member declarations of
// ArtClassDefinition are identical in Android 13--17 (arm64, libc++). Android
// 11--12 have a separate fixed declaration with two lazy MemMaps.
namespace artdcevm::abi::arm64::android13 {
using class_definition::LibcppString;
using class_definition::LibcppBytes;
// art::ArrayRef<const unsigned char>.
using class_definition::ByteRef;
struct ArtClassDefinition {
  jclass klass;
  jobject loader;
  LibcppString name;
  jobject protection_domain;
  LibcppBytes dex_data_memory;
  // GetNewDexData(): the bytes after every ClassFileLoadHook.
  ByteRef dex_data;
  LibcppBytes current_dex_memory;
  ByteRef current_dex_file;
  bool redefined;
  bool initialized;
  bool structural_transform_update;
};
static_assert(offsetof(ArtClassDefinition, dex_data) == 72);
static_assert(offsetof(ArtClassDefinition, initialized) == 129);
static_assert(sizeof(ArtClassDefinition) == 136);
// std::vector<ArtClassDefinition>.
using ClassDefinitions = class_definition::Vector<ArtClassDefinition>;
inline class_definition::View ReadClassDefinition(const void* address) {
  return class_definition::Read(*static_cast<const ArtClassDefinition*>(address));
}
inline constexpr class_definition::Layout kClassDefinitions{
    sizeof(ArtClassDefinition), ReadClassDefinition, false};
}  // namespace artdcevm::abi::arm64::android13
