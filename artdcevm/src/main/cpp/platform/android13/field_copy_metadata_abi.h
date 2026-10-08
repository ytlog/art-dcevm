#pragma once
#include "platform/field_array_view.h"
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13::field_copy_abi {
// runtime/art_field.h and libartbase/base/length_prefixed_array.h from the
// locked Android 13 release profile. GcRoot<Class> contains one uint32_t.
using ArtField = field_array::Field;
struct FieldArrayPrefix { uint32_t size; };
inline constexpr size_t kFieldArrayDataOffset = field_array::kDataOffset;
static_assert(sizeof(ArtField) == 16 && alignof(ArtField) == 4);
static_assert(offsetof(ArtField, offset) == 12);
}
