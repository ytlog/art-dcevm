#pragma once
#include <jni.h>
#include <cstddef>

// openjdkjvmti/ti_class_definition.h: the member declarations of
// ArtClassDefinition are identical in Android 13--17 (arm64, libc++). Android
// 11--12 also hold two MemMaps and lazily filled DEX memory, so this
// declaration never applies to them.
namespace artdcevm::abi::arm64::android13 {
struct LibcppString { void* words[3]; };
struct LibcppBytes { unsigned char* begin; unsigned char* end; unsigned char* capacity; };
// art::ArrayRef<const unsigned char>.
struct ByteRef { const unsigned char* data; size_t size; };
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
struct ClassDefinitions {
  const ArtClassDefinition* begin;
  const ArtClassDefinition* end;
  const void* capacity;
};
}  // namespace artdcevm::abi::arm64::android13
