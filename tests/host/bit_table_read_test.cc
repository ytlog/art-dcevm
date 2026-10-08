#include "platform/common/bit_table_read.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>
using artdcevm::bit_table_read::ReadCell;
using artdcevm::bit_table_read::Region;
int main() {
  std::array<unsigned char, 18000> bytes{};
  std::mt19937 random(0x4311);
  for (auto& byte : bytes) byte = static_cast<unsigned char>(random());
  for (unsigned trial = 0; trial < 20000; ++trial) {
    size_t width = random() % 33, stride = width + random() % 97;
    size_t column = random() % (stride - width + 1), start = random() % 17;
    uint32_t rows = 1 + random() % 100, row = random() % rows;
    Region region{bytes.data(), start, size_t{rows} * stride};
    uint32_t expected = 0;
    // Independent oracle: assemble the at-most-five source bytes into uint64,
    // shift to the requested little-endian slice, then apply AOSP value bias.
    size_t bit = start + size_t{row} * stride + column;
    if (width) {
      uint64_t word = 0;
      for (size_t i = 0; i < (bit % 8 + width + 7) / 8; ++i)
        word |= uint64_t{bytes[bit / 8 + i]} << (i * 8);
      expected = static_cast<uint32_t>((word >> (bit % 8)) & ((uint64_t{1} << width) - 1));
    }
    uint32_t got = 0;
    assert(ReadCell(region, rows, row, stride, column, width, &got));
    assert(got == expected - uint32_t{1});
  }
  // Android 17 final column exceeds uint16, with a uint32 row stride.
  uint32_t got = 0;
  assert(ReadCell({bytes.data(), 3, 140000}, 2, 1, 70000, 65534, 32, &got));
  uint32_t expected = 0;
  for (size_t bit = 0; bit < 32; ++bit) {
    size_t at = 3 + 70000 + 65534 + bit;
    expected |= uint32_t{(bytes[at / 8] >> (at % 8)) & 1u} << bit;
  }
  assert(got == expected - uint32_t{1});
  assert(ReadCell({nullptr, 0, 0}, 1, 0, 0, 0, 0, &got) && got == UINT32_MAX);
  unsigned char all = 255;
  assert(ReadCell({&all, 7, 1}, 1, 0, 1, 0, 1, &got) && got == 0);
  constexpr size_t max = std::numeric_limits<size_t>::max();
  auto rejected = [&](Region region, uint32_t rows, uint32_t row, size_t stride,
                      size_t column, size_t width) {
    got = 0x12345678;
    assert(!ReadCell(region, rows, row, stride, column, width, &got));
    assert(got == 0x12345678);
  };
  rejected({bytes.data(), 0, 8}, 0, 0, 8, 0, 1);
  rejected({bytes.data(), 0, 8}, 1, 1, 8, 0, 1);
  rejected({bytes.data(), 0, 33}, 1, 0, 33, 0, 33);
  rejected({bytes.data(), 0, 8}, 1, 0, 8, 9, 0);
  rejected({bytes.data(), 0, 8}, 1, 0, 8, 7, 2);
  rejected({bytes.data(), 0, max}, 4, 3, max / 2, 0, 1);
  rejected({bytes.data(), 0, max}, 3, 2, max / 2, 2, 1);
  rejected({bytes.data(), 0, 8}, 2, 1, 8, 0, 1);
  rejected({bytes.data(), 0, 7}, 1, 0, 8, 0, 8);
  rejected({bytes.data(), max, 8}, 1, 0, 8, 1, 1);
  rejected({bytes.data(), max, 8}, 1, 0, 8, 0, 2);
  rejected({nullptr, 0, 8}, 1, 0, 8, 0, 1);
  rejected({reinterpret_cast<void*>(uintptr_t{max}), 8, 8}, 1, 0, 8, 0, 1);
  assert(!ReadCell({bytes.data(), 0, 8}, 1, 0, 8, 0, 1, nullptr));
  // Exact byte allocation: no speculative ART-style word-sized overread.
  assert(ReadCell({&all, 0, 8}, 1, 0, 8, 0, 8, &got) && got == 254);
  puts("Bit-table bounded reader: passed 20000 oracle cases, modern stride, boundaries");
}
