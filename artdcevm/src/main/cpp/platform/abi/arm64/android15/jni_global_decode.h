#pragma once
#include <jni.h>
#include <cstdint>
#include <type_traits>
namespace artdcevm::abi::arm64::android15 {
// ObjPtr is a trivially-copyable uintptr_t scalar. For a separately calibrated
// release ART (kObjPtrPoisoning=false), reference is the unpoisoned object pointer.
// Debug builds require cookie decoding and are intentionally unsupported here.
struct ReleaseObjectPointer {uintptr_t reference;};
using DecodeGlobalJObject=ReleaseObjectPointer (*)(const void*,jobject);
inline constexpr const char* kDecodeGlobalJObject="_ZNK3art6Thread19DecodeGlobalJObjectEP8_jobject";
static_assert(sizeof(ReleaseObjectPointer)==8 && std::is_trivially_copyable_v<ReleaseObjectPointer>);
}
