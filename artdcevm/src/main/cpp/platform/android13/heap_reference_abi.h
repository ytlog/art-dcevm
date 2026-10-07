#pragma once

#include <cstddef>
#include <cstdint>

namespace artdcevm::android13::heap_reference_abi {
// Locked android-13.0.0_r1 runtime/mirror/object{,_reference}.h,
// runtime/lock_word.h and runtime/read_barrier.h, release Baker/no-poison profile.
struct ObjectPrefix {
  uint32_t klass;
  uint32_t monitor;
};
inline constexpr size_t kMonitorOffset = offsetof(ObjectPrefix, monitor);
inline constexpr uint32_t kGrayState = 1;
inline constexpr uint32_t kReadBarrierShift = 28;
inline constexpr uint32_t kReadBarrierMask = 1;
inline constexpr char kReadBarrierMark[] = "artReadBarrierMark";
static_assert(sizeof(ObjectPrefix) == 8);
static_assert(kMonitorOffset == 4);
}
