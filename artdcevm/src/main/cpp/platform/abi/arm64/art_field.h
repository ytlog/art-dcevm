#pragma once
#include <cstdint>
#include <cstddef>
namespace artdcevm::abi::arm64 {
// Fixed ArtField declaration shared by the audited release API 30–37 sources.
struct Field {
  uint32_t declaring_class, access_flags, dex_field_index, offset;
};
static_assert(sizeof(Field) == 16 && alignof(Field) == 4 && offsetof(Field, offset) == 12);
}
