#pragma once
#include "platform/abi/arm64/android14/jni_global_decode.h"
namespace artdcevm::abi::arm64::android15 {
// The fixed 15--17 global decoder uses the declaration introduced in 14.
using android14::ReleaseObjectPointer;
using android14::DecodeGlobalJObject;
using android14::kDecodeGlobalJObject;
}
