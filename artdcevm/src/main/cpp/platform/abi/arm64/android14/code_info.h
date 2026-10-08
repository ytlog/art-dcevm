#pragma once
#include "platform/abi/arm64/android13/code_info.h"
namespace artdcevm::abi::arm64::android14 {
// Fixed android-14.0.0_r1 runtime/stack_map.h and base/bit_table.h.
// Verified identical decoded fields/BitTable storage to 13; flags have their
// own source declaration. This alias does not select or authorize a device ABI.
using CodeInfo=android13::CodeInfo;
using CodeInfoPair=android13::CodeInfoPair;
using ConstructCodeInfo=android13::ConstructCodeInfo;
using FindStackMap=android13::FindStackMap;
inline constexpr const char* kConstructCodeInfo=android13::kConstructCodeInfo;
inline constexpr const char* kFindStackMap=android13::kFindStackMap;
static_assert(sizeof(CodeInfo)==344 && offsetof(CodeInfo,method_infos)==200);
}
