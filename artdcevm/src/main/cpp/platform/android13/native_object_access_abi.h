#pragma once
#include <cstddef>
#include <cstdint>

namespace artdcevm::android13::native_access_abi {
// runtime/jni/jni_env_ext.h and runtime/thread.h, release arm64 profile.
struct EnvironmentPrefix { const void* functions; void* self; void* vm; };
inline constexpr size_t kThreadFlagsOffset = 0;
inline constexpr unsigned kStateShift = 24;
inline constexpr uint32_t kRunnable = 0;
inline constexpr uint32_t kNative = 92;
inline constexpr char kDecodeJObject[] = "_ZNK3art6Thread13DecodeJObjectEP8_jobject";
inline constexpr char kSelfOffset[] = "_ZN3art9JNIEnvExt10SelfOffsetEm";
static_assert(offsetof(EnvironmentPrefix, self) == 8);
}
