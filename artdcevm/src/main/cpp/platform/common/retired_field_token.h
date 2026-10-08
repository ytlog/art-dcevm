#pragma once
#include <cstdint>
namespace artdcevm::retirement {
// Owned registry tokens occupy the high-bit namespace of Field.art_field_index.
inline constexpr uint32_t kRetiredFieldTag = uint32_t{1} << 31;
}
