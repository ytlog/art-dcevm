#pragma once
#include <cstdint>

namespace artdcevm::abi::arm64::android11 {
// Fixed runtime/runtime.h release declaration shared by Android 11–13.
inline constexpr char kIsAsyncDeoptimizeable[] =
    "_ZNK3art7Runtime21IsAsyncDeoptimizeableEm";
using IsAsyncDeoptimizeable = bool (*)(const void* runtime, uintptr_t code);
}
