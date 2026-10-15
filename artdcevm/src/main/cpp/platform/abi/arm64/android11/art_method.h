#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// art_method.h of android-11.0.0_r1 (34f62af4d6425087667b7eb3998fcf58d61a2fa6):
// dex_code_item_offset_ precedes dex_method_index_. Later releases drop it.
namespace artdcevm::abi::arm64::android11 {
struct ArtMethod {
  uint32_t declaring_class;
  std::atomic<uint32_t> access_flags;
  uint32_t dex_code_item_offset;
  uint32_t dex_method_index;
  uint16_t method_index;
  uint16_t hotness_count;
  void* data;
  void* quick_entry;
};
static_assert(offsetof(ArtMethod, dex_method_index) == 12 && sizeof(ArtMethod) == 40);

// art_method.h IsDefaultConflicting of android-11.0.0_r1 with its
// libdexfile/dex/modifiers.h values: a dedicated flag, never on an intrinsic.
inline bool IsDefaultConflicting(const ArtMethod& method) {
  constexpr uint32_t kAccDefaultConflict = 0x01000000, kAccIntrinsic = 0x80000000;
  const uint32_t flags = method.access_flags.load(std::memory_order_relaxed);
  return !(flags & kAccIntrinsic) && (flags & kAccDefaultConflict);
}
}  // namespace artdcevm::abi::arm64::android11
