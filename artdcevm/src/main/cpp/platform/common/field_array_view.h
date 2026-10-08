#pragma once
#include "platform/common/class_metadata_abi.h"
#include "platform/abi/arm64/art_field.h"
namespace artdcevm::field_array {
// Fixed runtime/art_field.h declaration, unchanged across release API 30–37.
// Access flags are ordinary uint32_t in ART (unlike ArtMethod). Caller keeps
// storage live under mutator access; structural mutation requires exclusive access.
using Field = abi::arm64::Field;
inline constexpr size_t kDataOffset = 4;
struct Layout {
  size_t class_size, instance_pointer, static_pointer;
  bool merged;
};
inline constexpr Layout kSplit{sizeof(class_metadata::Class11),
    offsetof(class_metadata::Class11, ifields), offsetof(class_metadata::Class11, sfields), false};
inline constexpr Layout kMerged16{sizeof(class_metadata::Class16),
    offsetof(class_metadata::Class16, fields), offsetof(class_metadata::Class16, fields), true};
inline constexpr Layout kMerged17{sizeof(class_metadata::Class17),
    offsetof(class_metadata::Class17, fields), offsetof(class_metadata::Class17, fields), true};
inline uint32_t Flags(const void* field) {
  uint32_t flags;
  std::memcpy(&flags, static_cast<const unsigned char*>(field) + 4, 4);
  return flags;
}
// Returns an opaque native pointer only. The source class declaration must be
// matched/calibrated; no managed reference or read barrier is bypassed here.
inline bool ReadPointer(const Layout& layout, const void* klass, size_t extent,
                        bool is_static, void** output) {
  const auto address = reinterpret_cast<uintptr_t>(klass);
  const size_t offset = is_static ? layout.static_pointer : layout.instance_pointer;
  if (!klass || !output || extent < layout.class_size || address % 4 ||
      layout.class_size < 8 || offset > layout.class_size - 8 ||
      address > UINTPTR_MAX - layout.class_size) return false;
  uint64_t pointer;
  std::memcpy(&pointer, static_cast<const unsigned char*>(klass) + offset, 8);
  *output = reinterpret_cast<void*>(static_cast<uintptr_t>(pointer)); return true;
}
struct View {
  void* array = nullptr;
  uint32_t count = 0, selected_count = 0;
  bool merged = false, is_static = false;
  // Physical indices belong to the actual LengthPrefixedArray. A filtered
  // ordinal must never be stored as a modern reflection Field's array index.
  void* At(uint32_t physical_index) const {
    return physical_index < count ? static_cast<unsigned char*>(array) + kDataOffset + size_t(physical_index) * sizeof(Field) : nullptr;
  }
  bool Selected(uint32_t index) const {
    return index < count && (!merged || ((Flags(At(index)) & 0x8u) != 0) == is_static);
  }
  void* AtSelected(uint32_t ordinal) const {
    if (ordinal >= selected_count) return nullptr;
    if (!merged) return At(ordinal);
    for (uint32_t i = 0; i < count; ++i) if (Selected(i)) {
      if (!ordinal) return At(i);
      --ordinal;
    }
    return nullptr;
  }
  bool IndexOf(const void* field, uint32_t* output) const {
    if (!array || !field || !output) return false;
    const auto begin = reinterpret_cast<uintptr_t>(array) + kDataOffset;
    const auto address = reinterpret_cast<uintptr_t>(field);
    if (address < begin || (address - begin) % sizeof(Field)) return false;
    const size_t index = (address - begin) / sizeof(Field);
    if (index >= count || !Selected(static_cast<uint32_t>(index))) return false;
    *output = static_cast<uint32_t>(index); return true;
  }
};
// Extent must describe verified live native storage, independent of its header.
// Legacy arrays are already separated; merged arrays filter ArtField::IsStatic.
inline bool Read(const Layout& layout, void* array, size_t extent, bool is_static, View* output) {
  if (!output) return false;
  const auto address = reinterpret_cast<uintptr_t>(array);
  uint32_t count = 0;
  if (array) {
    if (address % 4 || extent < kDataOffset || address > UINTPTR_MAX - kDataOffset) return false;
    std::memcpy(&count, array, 4);
    if (count > (extent - kDataOffset) / sizeof(Field) || count > (UINTPTR_MAX - address - kDataOffset) / sizeof(Field))
      return false;
  } else if (extent) return false;
  View result{array, count, count, layout.merged, is_static};
  if (result.merged) {
    result.selected_count = 0;
    for (uint32_t i = 0; i < count; ++i) result.selected_count += result.Selected(i);
  }
  *output = result; return true;
}
// Existing calibrated providers trust the ART-owned allocation's length header.
// This path cannot prove an independent mapped extent; use Read directly when
// one is available. No arbitrary/unmatched class or array may reach this helper.
inline bool ReadTrustedClass(const Layout& layout, const void* klass, size_t class_extent, bool is_static, View* output) {
  static_assert(sizeof(uintptr_t) == 8, "Fixed arm64 provider declaration");
  if (!output) return false;
  void* array = nullptr;
  if (!ReadPointer(layout, klass, class_extent, is_static, &array)) return false;
  const auto address = reinterpret_cast<uintptr_t>(array);
  if (array && (address % 4 || address > UINTPTR_MAX - kDataOffset)) return false;
  uint32_t count = 0;
  if (array) std::memcpy(&count, array, 4);
  const size_t extent = array ? kDataOffset + size_t(count) * sizeof(Field) : 0;
  return Read(layout, array, extent, is_static, output);
}
}
