#pragma once
#include "platform/abi/arm64/android12/frame_registers.h"
namespace artdcevm::abi::arm64::android14::frame_registers {
// Fixed android-14.0.0_r1: slow overload adds need_full_register_list.
using Kind=android12::frame_registers::Kind;
using Location=android12::frame_registers::Location;
using Slow=bool (*)(const void*,void*,uint16_t,Kind,uint32_t*,bool);
using Fast=android12::frame_registers::Fast;
inline constexpr char kSlow[]="_ZNK3art12StackVisitor24GetVRegFromOptimizedCodeEPNS_9ArtMethodEtNS_8VRegKindEPjb";
inline constexpr const char* kFast=android12::frame_registers::kFast;
}
