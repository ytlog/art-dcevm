#pragma once
#include <cstddef>
#include <array>
#include "platform/abi/arm64/android11/instrumentation.h"
#include "platform/abi/arm64/android13/instrumentation.h"
#include "platform/abi/arm64/android14/instrumentation.h"
namespace artdcevm::abi::arm64 {
// Native runtime object, not a managed reference. A public AddListener(0x8)
// 0->1 transition must independently select exactly one source declaration.
inline constexpr std::array<size_t,3> kDexPcListenerOffsets{
  offsetof(android11::Instrumentation,dex_pc), offsetof(android13::Instrumentation,dex_pc),
  offsetof(android14::Instrumentation,dex_pc)};
}
