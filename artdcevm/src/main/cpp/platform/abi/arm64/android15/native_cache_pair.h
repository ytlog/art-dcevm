#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android15 {
// Fixed source declaration; actual device Profile and live storage are prerequisites.
struct alignas(16) SequencePair { std::atomic<uint64_t> key,method; };
static_assert(sizeof(SequencePair)==16 && offsetof(SequencePair,method)==8);
inline constexpr uint64_t kSequenceMask=0xffffffff00000000ull;
inline constexpr uint64_t kSequenceLock=0x8000000000000000ull;

}
