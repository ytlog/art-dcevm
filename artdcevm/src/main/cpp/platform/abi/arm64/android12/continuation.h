#pragma once
#include "platform/abi/arm64/android11/continuation.h"
namespace artdcevm::abi::arm64::android12 {
// Fixed android-12.0.0_r1 and android-12.1.0_r1 retain the Android 11 ABI.
using ContinuationFrame = android11::ContinuationFrame;
using ContinuationAccessor = android11::ContinuationAccessor;
using MoveException = android11::MoveException;
}
