#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android13 {
// Fixed 13–17 thread.h: uint8_t state in high 8 bits; first Thread TLS word.
inline constexpr size_t kThreadFlagsOffset=0;
inline constexpr unsigned kStateShift=24;
inline constexpr uint32_t kRunnable=0,kNative=92;
// Pure transitions in these fixed source releases. These names are stripped on
// some devices; presence is independently required before a Runnable transition.
inline constexpr char kToNative[]="artJniMethodStart",kToRunnable[]="artJniMethodEnd";
using Transition=void (*)(void*);
}
