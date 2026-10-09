#pragma once
#include "platform/abi/arm64/android12/frame_registers.h"
namespace artdcevm::abi::arm64::android13 {
// Fixed android-13.0.0_r1 runtime/stack.h: same named register-read ABI.
namespace frame_registers=android12::frame_registers;
}
