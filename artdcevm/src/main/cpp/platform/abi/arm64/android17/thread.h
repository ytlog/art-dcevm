#pragma once
#include "platform/abi/arm64/android16/thread.h"
namespace artdcevm::abi::arm64::android17 {
// Fixed source: https://android.googlesource.com/platform/art/+/b753cf97923c3695338d21466fa14c57b480a59a/runtime/thread.h?format=TEXT
// Independent source-field offsetof audit confirms the entire TLS32/TLS64 and
// pointer prefix through suspend_trigger matches Android16. Share the physical
// declaration; providers must still calibrate each ART identity independently.
using ThreadTlsPrefix = android16::ThreadTlsPrefix;
}  // namespace artdcevm::abi::arm64::android17
