#pragma once
#include "platform/abi/arm64/android13/thread_state.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include "platform/abi/arm64/android11/native_offset.h"
#include <cstddef>
#include <cstdint>

namespace artdcevm::android13::native_access_abi {
// runtime/jni/jni_env_ext.h and runtime/thread.h, release arm64 profile.
struct EnvironmentPrefix { const void* functions; void* self; void* vm; };
inline constexpr size_t kThreadFlagsOffset = abi::arm64::android13::kThreadFlagsOffset;
inline constexpr unsigned kStateShift = abi::arm64::android13::kStateShift;
inline constexpr uint32_t kRunnable = abi::arm64::android13::kRunnable;
inline constexpr uint32_t kNative = abi::arm64::android13::kNative;
inline constexpr const char* kDecodeJObject=abi::arm64::android11::kDecodeJObject;
using DecodeObject=abi::arm64::android11::DecodeJObject;
inline constexpr const char* kSelfOffset=abi::arm64::android11::kSelfOffset;
using SelfOffset=abi::arm64::android11::SelfOffset;
static_assert(offsetof(EnvironmentPrefix, self) == 8);
}
