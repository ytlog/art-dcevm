#pragma once
#include "evolution/class_schema.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace artdcevm::dex_mapping_abi {
// Fixed release arm64 declarations from android-11.0.0_r1 through
// android-17.0.0_r1 libdexfile/dex/dex_file.h. API 34 changes the two data
// members to ArrayRef with the same pointer/size representation. API 35
// preserves the size slot for DRM but stops using it as a mapping extent.
// A matched device declaration and live DexFile lifetime remain caller duties.
struct Prefix {
  const void* vtable;
  const unsigned char* begin;
  size_t size;  // API 30-34: size_; API 35-37: unused_size_, never an extent.
  const unsigned char* data_begin;
  size_t data_size;
};
static_assert(sizeof(void*) == 8);
static_assert(sizeof(Prefix) == 40);
static_assert(offsetof(Prefix, begin) == 8);
static_assert(offsetof(Prefix, data_begin) == 24);

inline bool Normalize(int api, const Prefix& prefix, uint32_t class_index,
                      evolution::MappedDexMetadata* output, std::string* error) {
  auto fail = [&](const char* message) { if (error) *error = message; return false; };
  if (!output || api < 30 || api > 37) return fail("Unsupported DEX mapping declaration");
  const auto maximum = std::numeric_limits<uintptr_t>::max();
  const auto begin = reinterpret_cast<uintptr_t>(prefix.begin);
  const auto data = reinterpret_cast<uintptr_t>(prefix.data_begin);
  if (!begin || !data || prefix.data_size > maximum - data)
    return fail("Invalid DEX data mapping");
  size_t extent = prefix.size;
  if (api >= 35) {
    // Unlike the unused size slot, DataBegin/DataSize still describe a real
    // mapping. Validate containment before reading any bytes of this header.
    // Separate CompactDex header mappings need another calibrated extent source.
    if (begin < data || begin - data > prefix.data_size ||
        prefix.data_size - (begin - data) < 0x70)
      return fail("DEX header is outside the bounded data mapping");
    if (std::memcmp(prefix.begin, "dex\n", 4) != 0 || prefix.begin[7] != 0)
      return fail("Modern DEX mapping requires a bounded StandardDex header");
    const auto* bytes = prefix.begin + 32;
    extent = bytes[0] | (uint32_t{bytes[1]} << 8) |
        (uint32_t{bytes[2]} << 16) | (uint32_t{bytes[3]} << 24);
    if (extent > prefix.data_size - (begin - data))
      return fail("DEX file extent exceeds its data mapping");
  }
  if (extent < 0x70 || extent > maximum - begin)
    return fail("Invalid DEX header mapping extent");
  *output = {prefix.begin, extent, prefix.data_begin, prefix.data_size, class_index};
  if (error) error->clear();
  return true;
}
}  // namespace artdcevm::dex_mapping_abi
