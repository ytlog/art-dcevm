#pragma once
#include <cstddef>
#include <cstdint>
// runtime/interpreter/shadow_frame.h and lock_count_data.h of fixed
// android-16.0.0_r1 and android-17.0.0_r1, arm64/libc++.
namespace artdcevm::abi::arm64::android16 {
struct ShadowFrame {
  void* link;
  void* method;
  void* monitors;
  uint32_t number_of_vregs, dex_pc, frame_flags;
  uint32_t vregs[0];
};
static_assert(offsetof(ShadowFrame, number_of_vregs) == 24 && offsetof(ShadowFrame, vregs) == 36);
}
