#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace artdcevm::bit_table_read {
// Borrowed, immutable decoded storage. The provider must establish mapped memory
// and lifetime; arithmetic checks cannot prove the underlying allocation.
// This is a logical view, not a declaration of any ART BitTable/CodeInfo ABI.
struct Region {
  const void* data;
  size_t bit_start;
  size_t bit_size;
};

// Fixed AOSP 11--17 BitTableBase::Get loads little-endian bits and adds
// UINT32_MAX (zero encodes kNoValue). Android 17 stores row_bits_ separately
// and permits the final column end above UINT16_MAX; pass the provider's
// decoded stride/range without truncating them to the older ABI.
inline bool ReadCell(Region region, uint32_t rows, uint32_t row,
                     size_t row_bits, size_t column_offset, size_t width,
                     uint32_t* result) {
  constexpr size_t kMax = std::numeric_limits<size_t>::max();
  if (!result || row >= rows || width > 32 || column_offset > row_bits ||
      width > row_bits - column_offset || (row && row_bits > kMax / row))
    return false;
  size_t offset = size_t{row} * row_bits;
  if (column_offset > kMax - offset) return false;
  offset += column_offset;
  if (offset > region.bit_size || width > region.bit_size - offset ||
      region.bit_start > kMax - offset)
    return false;
  size_t start = region.bit_start + offset;
  if (width) {
    if (!region.data || width - 1 > kMax - start) return false;
    size_t last_byte = (start + width - 1) / 8;
    if (last_byte > std::numeric_limits<uintptr_t>::max() -
                        reinterpret_cast<uintptr_t>(region.data))
      return false;
  }
  uint32_t value = 0;
  auto* bytes = static_cast<const unsigned char*>(region.data);
  for (size_t bit = 0; bit < width; ++bit) {
    size_t at = start + bit;
    value |= uint32_t{(bytes[at / 8] >> (at % 8)) & 1u} << bit;
  }
  *result = value - uint32_t{1};
  return true;
}
}  // namespace artdcevm::bit_table_read
