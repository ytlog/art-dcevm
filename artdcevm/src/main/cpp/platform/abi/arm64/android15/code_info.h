#pragma once
#include "platform/abi/arm64/android13/code_info.h"
namespace artdcevm::abi::arm64::android15 {
// Fixed stack_map.h CodeInfo::kHasInlineInfo (positive native WalkStack oracle only).
inline constexpr uint32_t kHasInlineInfo=1u<<0;
// Fixed android-15.0.0_r1 runtime/oat/stack_map.h: decoded storage same13.
using CodeInfo=android13::CodeInfo;
using CodeInfoPair=android13::CodeInfoPair;
}
