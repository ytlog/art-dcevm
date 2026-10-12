#pragma once
#include <cstdint>
#include <cstring>
#include "evolution/class_schema.h"
#include "platform/abi/arm64/common/redefinition.h"

namespace artdcevm {
// An ART DexFile as the bounds-checked view read by evolution::Read*.
// The file size comes from the mapped header, not from a version-specific member.
inline evolution::DexView DexViewOf(const abi::arm64::DexFilePrefix& dex) {
  uint32_t file_size = 0;
  if (dex.begin)
    std::memcpy(&file_size, dex.begin + abi::arm64::kDexHeaderFileSizeOffset, sizeof(file_size));
  return {dex.begin, file_size, dex.data_begin, dex.data_size};
}
}  // namespace artdcevm
