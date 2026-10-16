#include "platform/common/runtime_thread.h"

#include <android/log.h>
#include <array>
#include <cstdlib>
#include <sys/syscall.h>
#include <unistd.h>
#include "platform/abi/arm64/android11/thread.h"
#include "platform/abi/arm64/android12/thread.h"
#include "platform/abi/arm64/android13/thread.h"
#include "platform/abi/arm64/android14/thread.h"
#include "platform/abi/arm64/android15/thread.h"
#include "platform/abi/arm64/android16/thread.h"
#include "platform/abi/arm64/android17/thread.h"
#include "platform/common/class_layout.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/runtime_thread_symbols.h"

namespace artdcevm::runtime_thread {
namespace {
using namespace abi::arm64;
void* (*current_thread)() = nullptr;
void* (*mark)(void*) = nullptr;
const bool* use_read_barrier = nullptr;
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
thread_local bool reject_next_probe = false;
thread_local uint64_t reads = 0, marking_reads = 0;
#endif
template <class T> T Load(void* thread, size_t offset) noexcept {
  return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(thread) + offset);
}
constexpr std::array<RuntimeThread, 7> kLayouts = {{
    RuntimeThreadFor<android11::Thread>(), RuntimeThreadFor<android12::Thread>(),
    RuntimeThreadFor<android13::Thread>(), RuntimeThreadFor<android14::Thread>(),
    RuntimeThreadFor<android15::Thread>(), RuntimeThreadFor<android16::Thread>(),
    RuntimeThreadFor<android17::Thread>()}};
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
    for (int release : class_layout::CandidateReleases(false)) {
      const auto& candidate = kLayouts[release - 11];
      if (!next->thread || Load<void*>(next->thread, candidate.jni_env) != jni ||
          Load<uint32_t>(next->thread, candidate.tid) != tid ||
          Load<uint32_t>(next->thread, candidate.is_gc_marking) > 1) continue;
      next->accepted |= uint32_t{1} << (release - 11);
      if (!next->layout) next->layout = &candidate;
    }
    if (next->layout) next->rejection = nullptr;
  }
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  if (reject_next_probe) {
    reject_next_probe = false;
    next->layout = nullptr;
    next->accepted = 0;
    next->rejection = "runtime Thread probe rejected injected mismatch";
  }
#endif
  context->thread = std::move(next);
  if (!Ready()) class_layout::Report(std::string("rejected: ") + Rejection());
}
bool Ready() noexcept { const auto* state = Get(); return state && state->layout; }
const char* Rejection() noexcept {
  const auto* state = Get();
  return state ? state->rejection : "runtime Thread was not probed in Native state";
}
bool Supports(int release) noexcept {
  const auto* state = Get();
  return state && release >= 11 && release <= 17 &&
      (state->accepted & (uint32_t{1} << (release - 11)));
}
void* Current() noexcept { const auto* state = Get(); return state ? state->thread : nullptr; }
const RuntimeThread& Layout() noexcept {
  if (!Ready()) UnverifiedRead();
  return *Get()->layout;
}
void* Read(uint32_t reference) noexcept {
  if (!reference) return nullptr;
  if (!Ready()) UnverifiedRead();
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  ++reads;
  if ((!use_read_barrier || *use_read_barrier) &&
      Load<uint32_t>(Get()->thread, Get()->layout->is_gc_marking)) ++marking_reads;
#endif
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
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_rejectRuntimeThreadProbe(JNIEnv*, jclass) {
  reject_next_probe = true;
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_runtimeReferenceReads(JNIEnv*, jclass) {
  return static_cast<jlong>(reads);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_InterfaceAbortInstrumentation_runtimeMarkingReads(JNIEnv*, jclass) {
  return static_cast<jlong>(marking_reads);
}
#endif
}  // namespace artdcevm::runtime_thread
