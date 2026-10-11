#pragma once
#include <cstdint>

namespace artdcevm::abi::arm64::android_common::method_compilation_policy {
// Android 16 runtime/art_method.h, mirror/class.cc and dex/modifiers.h.
// The intrinsic overlay must be cleared by ART itself: it also reconstructs
// hidden-API flags. Writing only kIntrinsic would corrupt that encoding.
enum class PointerSize:uint32_t {k32=4,k64=8};
using ClearClass=void(*)(void*,PointerSize);
using ClearIntrinsic=void(*)(void*);
inline constexpr char kClearClass[]="_ZN3art6mirror5Class32ClearDontCompileFlagOnAllMethodsENS_11PointerSizeE";
inline constexpr char kClearIntrinsic[]="_ZN3art9ArtMethod15SetNotIntrinsicEv";
inline constexpr uint32_t kDontCompile=0x02000000u;
inline constexpr uint32_t kPreCompiled=0x00800000u;
inline constexpr uint32_t kIntrinsic=0x80000000u;
inline constexpr uint32_t kNotManagedInvokable=0x500u;
}
