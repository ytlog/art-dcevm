#include "platform/common/runtime_thread.h"

#include <android/log.h>
#include <android/api-level.h>
#include <cstdlib>
#include <sys/syscall.h>
#include <unistd.h>
#include "platform/abi/arm64/release_order.h"
#include "platform/abi/arm64/runtime_thread_table.h"
#include "platform/common/fault_injection.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/runtime_thread_symbols.h"

namespace artdcevm::runtime_thread {
namespace {
using namespace abi::arm64;
void* (*current_thread)() = nullptr;
void* (*mark)(void*) = nullptr;
const bool* use_read_barrier = nullptr;
template <class T> T Load(void* thread, size_t offset) noexcept {
  return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(thread) + offset);
}
struct State final : transaction::Part {
  void* thread = nullptr;
  const RuntimeThread* layout = nullptr;
  uint32_t accepted = 0;
  const char* rejection = "runtime Thread declaration did not match JNI environment/tid/marking flag";
};
State* Get() noexcept {
  return transaction::PartOf<State>(transaction::Current(), &transaction::Context::thread);
}
[[noreturn]] void UnverifiedRead() noexcept {
  __android_log_write(ANDROID_LOG_FATAL, "ART-DCEVM", "heap reference read requires a verified runtime Thread");
  std::abort();
}
}  // namespace
void Prepare() {
  using namespace symbols::runtime_thread;
  current_thread = reinterpret_cast<decltype(current_thread)>(symbols::Find(kCurrentThread));
  mark = reinterpret_cast<decltype(mark)>(symbols::Find(kReadBarrierMark));
  use_read_barrier = static_cast<const bool*>(symbols::Find(kUseReadBarrier));
}
bool Available() { return current_thread && mark; }
void Probe(JNIEnv* jni) {
  auto* context = transaction::Current();
  if (!context) return;
  auto next = std::make_unique<State>();
  if (!Available()) {
    next->rejection = "runtime Thread/ReadBarrier named services unavailable";
  } else {
    next->thread = current_thread();
    const uint32_t tid = static_cast<uint32_t>(syscall(__NR_gettid));
    for (int release : CandidateReleases(android_get_device_api_level())) {
      const auto& candidate = *ThreadLayoutFor(release);
      if (!next->thread || Load<void*>(next->thread, candidate.jni_env) != jni ||
          Load<uint32_t>(next->thread, candidate.tid) != tid ||
          Load<uint32_t>(next->thread, candidate.is_gc_marking) > 1) continue;
      next->accepted |= uint32_t{1} << (release - 11);
      if (!next->layout) next->layout = &candidate;
    }
    if (next->layout) next->rejection = nullptr;
  }
  if (ARTDCEVM_REJECT_THREAD_PROBE()) {
    next->layout = nullptr;
    next->accepted = 0;
    next->rejection = "runtime Thread probe rejected injected mismatch";
  }
  context->thread = std::move(next);
}
std::optional<VerifiedThread> Ready() noexcept {
  const auto* state = Get();
  return state && state->layout ? std::optional<VerifiedThread>(VerifiedThread(state)) : std::nullopt;
}
const char* Rejection() noexcept {
  const auto* state = Get();
  return state ? state->rejection : "runtime Thread was not probed in Native state";
}
void* Current(const VerifiedThread& thread) noexcept {
  const auto* state = Get();
  if (!state || state != thread.state_ || !state->layout) UnverifiedRead();
  return state->thread;
}
const RuntimeThread& Layout(const VerifiedThread& thread) noexcept {
  Current(thread);
  return *Get()->layout;
}
const RuntimeThread* LayoutFor(const VerifiedThread& thread, int release) noexcept {
  Current(thread);
  return release >= 11 && release <= 17 &&
      (Get()->accepted & (uint32_t{1} << (release - 11))) ? ThreadLayoutFor(release) : nullptr;
}
void* Read(const VerifiedThread& thread, uint32_t reference) noexcept {
  Current(thread);
  if (!reference) return nullptr;
  ARTDCEVM_TEST(fault_injection::ReferenceRead((!use_read_barrier || *use_read_barrier) &&
      Load<uint32_t>(Get()->thread, Get()->layout->is_gc_marking));)
  auto* object = reinterpret_cast<void*>(static_cast<uintptr_t>(reference));
  // ART 14--17 read_barrier_config.h / mark_compact.cc: gUseReadBarrier
  // excludes userfaultfd CMC. CMC updates roots at its pause and resolves
  // compacted pages through faults; it must not call CC's Mark. On CC,
  // MarkFromReadBarrier itself tests the actual thread's marking flag. Calling
  // it unconditionally also avoids ambiguity between source Thread prefixes
  // that share JNI/tid slots on an updated ART module.
  if (use_read_barrier) return *use_read_barrier ? mark(object) : object;
  // ART 11--17 thread.h: is_gc_marking is CC-only; false is also the raw
  // BarrierForRoot fast path. This covers compile-time collector selection
  // (including forced CC builds without an exported gUseReadBarrier).
  const auto* state = Get();
  return Load<uint32_t>(state->thread, state->layout->is_gc_marking) ? mark(object) : object;
}
}  // namespace artdcevm::runtime_thread
