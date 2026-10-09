#pragma once
#include "platform/abi/arm64/android14/frame_registers.h"
namespace artdcevm::abi::arm64::android17 {
// Fixed b753cf97923c3695338d21466fa14c57b480a59a runtime/stack.h: same named register-read ABI.
namespace frame_registers=android14::frame_registers;
}
