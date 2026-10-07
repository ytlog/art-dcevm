#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13::field_copy_abi {
// runtime/art_field.h and libartbase/base/length_prefixed_array.h from the
// locked Android 13 release profile. GcRoot<Class> contains one uint32_t.
struct ArtField {
  uint32_t declaring_class;
  uint32_t access_flags;
  uint32_t dex_field_index;
  uint32_t offset;
};
struct FieldArrayPrefix { uint32_t size; };
inline constexpr size_t kFieldArrayDataOffset = 4;
static_assert(sizeof(ArtField) == 16 && alignof(ArtField) == 4);
static_assert(offsetof(ArtField, offset) == 12);
}
