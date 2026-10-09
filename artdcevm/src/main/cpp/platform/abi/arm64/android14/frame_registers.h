#pragma once
#include <optional>
#include <type_traits>
#include "platform/abi/arm64/android12/frame_registers.h"
namespace artdcevm::abi::arm64::android14::frame_registers {
// Fixed android-14.0.0_r1: slow overload adds need_full_register_list.
using Kind=android12::frame_registers::Kind;
using Location=android12::frame_registers::Location;
using Slow=bool (*)(const void*,void*,uint16_t,Kind,uint32_t*,bool);
using Fast=android12::frame_registers::Fast;
// Fixed runtime/stack.h public optional-location overload. DexRegisterLocation
// has a trivial 8-byte kind/value copy; libc++ optional stores that trivial
// value plus engagement, passed as the 12-byte trivial AAPCS64 aggregate.
using OptionalLocation=std::optional<Location>;
static_assert(sizeof(OptionalLocation)==12 && alignof(OptionalLocation)==4);
static_assert(std::is_trivially_copy_constructible_v<OptionalLocation> &&
              std::is_trivially_destructible_v<OptionalLocation>);
using Canonical=bool (*)(const void*,void*,uint16_t,Kind,uint32_t*,OptionalLocation,bool);
using DebuggerValue=bool (*)(const void*,uint16_t,Kind,uint32_t*);
using NativePcOffset=size_t (*)(const void*);
using AccessibleRegister=bool (*)(const void*,uint32_t,int32_t,uint32_t*);
inline constexpr char kCanonical[]="_ZNK3art12StackVisitor7GetVRegEPNS_9ArtMethodEtNS_8VRegKindEPjNSt3__18optionalINS_19DexRegisterLocationEEEb";
inline constexpr char kDebuggerValue[]="_ZNK3art12StackVisitor30GetVRegFromDebuggerShadowFrameEtNS_8VRegKindEPj";
inline constexpr char kNativePcOffset[]="_ZNK3art12StackVisitor17GetNativePcOffsetEv";
inline constexpr char kAccessibleRegister[]="_ZNK3art12StackVisitor23GetRegisterIfAccessibleEjNS_19DexRegisterLocation4KindEPj";
inline constexpr char kSlow[]="_ZNK3art12StackVisitor24GetVRegFromOptimizedCodeEPNS_9ArtMethodEtNS_8VRegKindEPjb";
inline constexpr const char* kFast=android12::frame_registers::kFast;
}
