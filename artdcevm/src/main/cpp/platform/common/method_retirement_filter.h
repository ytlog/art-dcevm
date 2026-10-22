#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace artdcevm::member_retirement {
// Monotonic negative filter for process-lifetime retired method identities.
// A hit is never a verdict: readers still query the locked authoritative set.
// Publish both bits before inserting a method. Failed insertions, collisions
// and saturation only cause extra set lookups; bits are never cleared.
class MethodRetirementFilter {
 public:
  MethodRetirementFilter() { for (auto& word : words_) word.store(0, std::memory_order_relaxed); }
  void Add(const void* method) noexcept {
    const uint64_t hash = Hash(method);
    Set(hash & kMask); Set((hash >> 32) & kMask);
  }
  bool MayContain(const void* method) const noexcept {
    const uint64_t hash = Hash(method);
    return Test(hash & kMask) && Test((hash >> 32) & kMask);
  }
 private:
  static constexpr uint64_t kMask = 8191;
  static uint64_t Hash(const void* method) noexcept {
    uint64_t value = reinterpret_cast<uintptr_t>(method);
    value ^= value >> 33; value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33; value *= UINT64_C(0xc4ceb9fe1a85ec53);
    return value ^ (value >> 33);
  }
  void Set(uint64_t bit) noexcept {
    words_[bit >> 6].fetch_or(UINT64_C(1) << (bit & 63), std::memory_order_seq_cst);
  }
  bool Test(uint64_t bit) const noexcept {
    return (words_[bit >> 6].load(std::memory_order_seq_cst) & (UINT64_C(1) << (bit & 63))) != 0;
  }
  std::array<std::atomic<uint64_t>, 128> words_;
};
}
