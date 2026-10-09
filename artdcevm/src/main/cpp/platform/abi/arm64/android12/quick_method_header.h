#pragma once
#include <cstdint>
namespace artdcevm::abi::arm64::android12 {
// Fixed android-12.0.0_r1/runtime/oat_quick_method_header.h compact data word.
struct QuickMethodHeader {uint32_t data;};
static_assert(sizeof(QuickMethodHeader)==4);
inline constexpr uint32_t kShouldDeoptimizeMask=0x80000000u;
}
