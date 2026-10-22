#pragma once
#include "platform/abi/arm64/android13/continuation.h"
namespace artdcevm::abi::arm64::android11 {
// Fixed android-11.0.0_r1: the frame/accessor declarations are byte-identical
// to android-13.0.0_r1. Exception delivery has a different native signature.
using ContinuationFrame = android13::ShadowFrame;
using ContinuationAccessor = android13::CodeItemDataAccessor;
using MoveException = bool (*)(void*, void*, const void*);
}
