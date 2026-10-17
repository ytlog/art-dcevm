#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
// Source-declared class layouts; runtime probes select among them.
#include "platform/abi/arm64/android11/art_method.h"
#include "platform/abi/arm64/android11/class.h"
#include "platform/abi/arm64/android12/class.h"
#include "platform/abi/arm64/android13/class.h"
#include "platform/abi/arm64/android14/class.h"
#include "platform/abi/arm64/android15/class.h"
#include "platform/abi/arm64/android16/class.h"
#include "platform/abi/arm64/android17/class.h"

namespace artdcevm::abi::arm64 {
constexpr size_t kLayoutCount = 7;
using LayoutMask = uint32_t;
static_assert(kLayoutCount <= 32);

// One entry per AOSP release whose declarations are in platform/abi/arm64:
// Ops<Class, Method>::Make(release) for each, in release order.
template <template <typename, typename> class Ops, typename Entry>
constexpr std::array<Entry, kLayoutCount> Table() {
  return {{Ops<android11::Class, android11::ArtMethod>::Make(11),
           Ops<android12::Class, ArtMethod>::Make(12),
           Ops<android13::Class, ArtMethod>::Make(13),
           Ops<android14::Class, ArtMethod>::Make(14),
           Ops<android15::Class, ArtMethod>::Make(15),
           Ops<android16::Class, ArtMethod>::Make(16),
           Ops<android17::Class, ArtMethod>::Make(17)}};
}
}  // namespace artdcevm::abi::arm64
