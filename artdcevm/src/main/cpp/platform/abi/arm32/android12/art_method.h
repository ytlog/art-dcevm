#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm32::android12 {
// android-12.0.0_r1 runtime/art_method.h: image PointerSize::k32.
// Fixed layout family also present in android-13.0.0_r1 and android-17.0.0_r1;
// selecting a live declaration still requires independent runtime calibration.
struct Method12 {
  uint32_t declaring_class, access_flags, dex_method_index;
  uint16_t method_index, hotness_count;
  uint32_t data, entry_point;
};
static_assert(sizeof(Method12)==24 && alignof(Method12)==4);
static_assert(offsetof(Method12,data)==16 && offsetof(Method12,entry_point)==20);
}
