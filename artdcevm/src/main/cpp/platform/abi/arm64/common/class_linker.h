#pragma once
#include <cstdint>
namespace artdcevm::abi::arm64 {
// Identical release declarations in checked Android 13 and 16 sources:
// libdexfile/dex/dex_file_types.h.
struct TypeIndex { uint16_t index; };
static_assert(sizeof(TypeIndex) == 2);
}  // namespace artdcevm::abi::arm64
