#pragma once
#include "platform/abi/arm64/android13/thread_state.h"
#include <cstddef>
#include <cstdint>

namespace artdcevm::android13::native_access_abi {
// runtime/jni/jni_env_ext.h and runtime/thread.h, release arm64 profile.
struct EnvironmentPrefix { const void* functions; void* self; void* vm; };
inline constexpr size_t kThreadFlagsOffset = abi::arm64::android13::kThreadFlagsOffset;
inline constexpr unsigned kStateShift = abi::arm64::android13::kStateShift;
inline constexpr uint32_t kRunnable = abi::arm64::android13::kRunnable;
inline constexpr uint32_t kNative = abi::arm64::android13::kNative;
inline constexpr char kDecodeJObject[] = "_ZNK3art6Thread13DecodeJObjectEP8_jobject";
inline constexpr char kSelfOffset[] = "_ZN3art9JNIEnvExt10SelfOffsetEm";
static_assert(offsetof(EnvironmentPrefix, self) == 8);
}
