#pragma once
#include <cstddef>
#include <cstdint>
// runtime/interpreter/shadow_frame.h and lock_count_data.h of fixed
// android-11.0.0_r1 through android-15.0.0_r1, arm64/libc++.
namespace artdcevm::abi::arm64::android13 {
struct ShadowFrame {
  void* link;
  void* method;
  void* result_register;
  const uint16_t* dex_pc_ptr;
  const uint16_t* dex_instructions;
  void* monitors;  // LockCountData's unique_ptr<vector<Object*>>.
  uint32_t number_of_vregs, dex_pc;
  int16_t cached_hotness_countdown, hotness_countdown;
  uint32_t frame_flags;
  uint32_t vregs[0];
};
static_assert(offsetof(ShadowFrame, number_of_vregs) == 48 && offsetof(ShadowFrame, vregs) == 64);
}
