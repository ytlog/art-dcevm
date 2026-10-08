#pragma once
#include "platform/abi/arm64/android11/jni_object_decode.h"
namespace artdcevm::abi::arm64::android14 {
// 14 moves local/transition decoding inline; this named leaf decodes globals.
using ReleaseObjectPointer=android11::ReleaseObjectPointer;
using DecodeGlobalJObject=ReleaseObjectPointer (*)(const void*,jobject);
inline constexpr const char* kDecodeGlobalJObject="_ZNK3art6Thread19DecodeGlobalJObjectEP8_jobject";
}
