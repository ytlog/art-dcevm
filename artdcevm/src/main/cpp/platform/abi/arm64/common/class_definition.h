#pragma once
#include <jni.h>
#include <cstddef>
#include <cstdint>

namespace artdcevm::abi::arm64::class_definition {
struct LibcppString { void* words[3]; };
struct LibcppBytes { unsigned char* begin; unsigned char* end; unsigned char* capacity; };
struct ByteRef { const unsigned char* data; size_t size; };
template <typename Definition> struct Vector {
  const Definition* begin;
  const Definition* end;
  const void* capacity;
};
using Definitions = Vector<void>;
static_assert(sizeof(Definitions) == 24);

// A non-owning view of fixed declarations. Never construct or destroy ART's
// strings, vectors or MemMaps. Read bool representations before accepting them.
struct View {
  jclass klass;
  ByteRef dex, current;
  uint8_t redefined, initialized, structural, from_class_ext;
  const unsigned char* lazy_begin = nullptr;
  size_t lazy_size = 0;
  bool lazy = false, can_initialize = false;
};
template <typename Definition> View Read(const Definition& value) {
  const auto flag = [](const bool& v) { return *reinterpret_cast<const uint8_t*>(&v); };
  return {value.klass, value.dex_data, value.current_dex_file,
          flag(value.redefined), flag(value.initialized), flag(value.structural_transform_update), 0};
}
struct Layout {
  size_t stride;
  View (*read)(const void*);
  bool lazy_memory;
};
}  // namespace artdcevm::abi::arm64::class_definition
