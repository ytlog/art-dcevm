#pragma once
#include "platform/common/code_info_flags.h"
namespace artdcevm::android13::code_info_flags_abi {
// CAS SDK33 matched profile: primary source
// bf4f61e6f7ceea1a00494d543e8cc13b20dd443f; device decoder flags calibrated
// 2026-10-07 (optimized inline=3, baseline=6). This is not android-13.0.0_r1:
// should-deoptimize=2 and baseline=4 differ from that upstream release family.
// Only the three proven meanings are declared. Higher flags remain unknown.
inline constexpr code_info_flags::Schema kMatchedCasSchema{1, 4, 2, 0, 0};
inline code_info_flags::State DecodeMatchedCas(uint32_t decoded_flags) {
  return code_info_flags::Decode(kMatchedCasSchema, decoded_flags);
}
}  // namespace artdcevm::android13::code_info_flags_abi
