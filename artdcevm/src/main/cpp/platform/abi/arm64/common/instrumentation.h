#pragma once
#include <cstddef>
#include <array>
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/abi/arm64/android11/instrumentation.h"
#include "platform/abi/arm64/android13/instrumentation.h"
#include "platform/abi/arm64/android14/instrumentation.h"
namespace artdcevm::abi::arm64 {
// runtime/instrumentation.h, Android 11--17 Itanium vtable prefix. Only the
// subscribed DexPcMoved slot is callable. The preceding unsubscribed slots
// preserve their order while their argument declarations vary across ARTs.
class DexPcListener {
 public:
  virtual ~DexPcListener() = default;
  virtual void UnusedEntry() {}
  virtual void UnusedExitObject() {}
  virtual void UnusedExitValue() {}
  virtual void UnusedUnwind() {}
  virtual void DexPcMoved(void*, Handle, void*, uint32_t) {}
};
// Native runtime object, not a managed reference. A public AddListener(0x8)
// 0->1 transition must independently select exactly one source declaration.
inline constexpr std::array<size_t,3> kDexPcListenerOffsets{
  offsetof(android11::Instrumentation,dex_pc), offsetof(android13::Instrumentation,dex_pc),
  offsetof(android14::Instrumentation,dex_pc)};
}
