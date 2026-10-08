#pragma once
#include <cstdint>

namespace artdcevm::abi::arm64::android14 {
// Fixed runtime/runtime.h release declaration shared by Android 14–17.
inline constexpr char kIsAsyncDeoptimizeable[] =
    "_ZNK3art7Runtime21IsAsyncDeoptimizeableEPNS_9ArtMethodEm";
using IsAsyncDeoptimizeable = bool (*)(const void* runtime, void* method, uintptr_t code);
}
