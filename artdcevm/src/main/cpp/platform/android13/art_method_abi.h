#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13::art_method_abi {
// Locked Android 13 runtime/art_method.h, 64-bit runtime pointer size.
struct ArtMethod {
  uint32_t declaring_class;
  uint32_t access_flags;
  uint32_t dex_method_index;
  uint16_t method_index;
  uint16_t hotness_count;
  uint64_t data;
  uint64_t entry_point;
};
inline constexpr size_t kArrayDataOffset = 8;
static_assert(sizeof(ArtMethod) == 32 && alignof(ArtMethod) == 8);
static_assert(offsetof(ArtMethod, access_flags) == 4);
static_assert(offsetof(ArtMethod, dex_method_index) == 8);
}
