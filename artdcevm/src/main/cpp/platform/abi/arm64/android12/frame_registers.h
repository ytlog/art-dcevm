#pragma once
#include "platform/abi/arm64/android11/frame_registers.h"
namespace artdcevm::abi::arm64::android12::frame_registers {
// Fixed android-12.0.0_r1: location overload drops the VRegKind argument.
using Kind=android11::frame_registers::Kind;
using Location=android11::frame_registers::Location;
using Slow=android11::frame_registers::Slow;
using Fast=bool (*)(const void*,Location,uint32_t*);
inline constexpr const char* kSlow=android11::frame_registers::kSlow;
inline constexpr char kFast[]="_ZNK3art12StackVisitor24GetVRegFromOptimizedCodeENS_19DexRegisterLocationEPj";
}
