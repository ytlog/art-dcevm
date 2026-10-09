#pragma once
#include "platform/abi/arm64/android14/frame_registers.h"
namespace artdcevm::abi::arm64::android16 {
// Fixed android-16.0.0_r1 runtime/stack.h: same named register-read ABI.
namespace frame_registers=android14::frame_registers;
}
