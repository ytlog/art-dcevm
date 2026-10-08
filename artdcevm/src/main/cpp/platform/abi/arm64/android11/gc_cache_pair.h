#pragma once
#include <atomic>
#include <cstdint>
namespace artdcevm::abi::arm64::android11 {
// Fixed DexCachePair<GcRoot<Class>> source family, Android11-17. The root
// is opaque compressed bits; a matched provider supplies its named barrier.
struct alignas(8) GcCachePair { uint32_t object,index; };
static_assert(sizeof(GcCachePair)==8 && std::atomic<GcCachePair>::is_always_lock_free);
inline constexpr uint32_t kTypeCacheSlots=1024;
}
