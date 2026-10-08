#pragma once
#include <jni.h>
#include <cstdint>
#include <type_traits>
namespace artdcevm::abi::arm64::android11 {
// Fixed ObjPtr<mirror::Object> release/no-poison carrier (11--17).
// Its use requires an independently matched release representation and a live
// Runnable/mutator scope. Debug poisoning cookies are not supported.
struct ReleaseObjectPointer {uintptr_t reference;};
using DecodeJObject=ReleaseObjectPointer (*)(const void*,jobject);
inline constexpr const char* kDecodeJObject="_ZNK3art6Thread13DecodeJObjectEP8_jobject";
static_assert(sizeof(ReleaseObjectPointer)==8&&alignof(ReleaseObjectPointer)==8&&
              std::is_trivially_copyable_v<ReleaseObjectPointer>);
}
