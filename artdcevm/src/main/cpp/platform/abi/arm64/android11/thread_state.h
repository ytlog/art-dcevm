#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android11 {
// Fixed 11–12.1 thread.h: low 16 flags, high 16 ThreadState; first Thread TLS word.
inline constexpr size_t kThreadFlagsOffset=0;
inline constexpr unsigned kStateShift=16;
inline constexpr uint32_t kRunnable=67,kNative=92;
}
