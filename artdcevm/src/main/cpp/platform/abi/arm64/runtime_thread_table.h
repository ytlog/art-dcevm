#pragma once
#include <array>
#include "platform/abi/arm64/common/runtime_thread.h"
#include "platform/abi/arm64/android11/thread.h"
#include "platform/abi/arm64/android12/thread.h"
#include "platform/abi/arm64/android13/thread.h"
#include "platform/abi/arm64/android14/thread.h"
#include "platform/abi/arm64/android15/thread.h"
#include "platform/abi/arm64/android16/thread.h"
#include "platform/abi/arm64/android17/thread.h"

namespace artdcevm::abi::arm64 {
inline constexpr std::array<RuntimeThread, 7> kRuntimeThreads = {{
    RuntimeThreadFor<android11::Thread>(),
    RuntimeThreadFor<android12::Thread>(),
    RuntimeThreadFor<android13::Thread>(),
    RuntimeThreadFor<android14::Thread>(),
    RuntimeThreadFor<android15::Thread>(),
    RuntimeThreadFor<android16::Thread>(),
    RuntimeThreadFor<android17::Thread>()}};
constexpr const RuntimeThread* ThreadLayoutFor(int release) {
  return release >= 11 && release <= 17 ? &kRuntimeThreads[release - 11] : nullptr;
}
constexpr bool CompatibleThreadPrefixes(const std::array<RuntimeThread, 7>& layouts) {
  for (const auto& first : layouts)
    for (const auto& second : layouts)
      if (first.jni_env == second.jni_env &&
          (first.exception != second.exception || first.top_handle_scope != second.top_handle_scope))
        return false;
  return true;
}
static_assert(CompatibleThreadPrefixes(kRuntimeThreads),
              "Thread declarations sharing JNI env must share exception and HandleScope slots");
}  // namespace artdcevm::abi::arm64
