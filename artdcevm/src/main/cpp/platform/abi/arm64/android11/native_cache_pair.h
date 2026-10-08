#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android11 {
// Fixed source declaration; actual device Profile and live storage are prerequisites.
struct alignas(16) Pair { uintptr_t method; uintptr_t index; };
static_assert(sizeof(Pair)==16 && std::atomic<Pair>::is_always_lock_free);

}
