#pragma once
#include <cstdint>

namespace artdcevm::abi::arm64 {
// runtime/jvalue.h, fixed Android 11--17: a trivially copied eight-byte union.
// AAPCS64 returns this non-HFA union in x0, including floating result bits.
struct JniInvokeValue { uint64_t bits = 0; };
static_assert(sizeof(JniInvokeValue) == 8 && alignof(JniInvokeValue) == 8);
}  // namespace artdcevm::abi::arm64
