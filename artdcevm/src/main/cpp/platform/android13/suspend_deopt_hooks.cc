#include "platform/android13/suspend_deopt_hooks.h"
#include "platform/android13/pending_suspend_deopt.h"
#include "platform/android13/suspend_deopt_art_abi.h"

#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
using SuspendFromCode = void (*)(void*);
SuspendFromCode original_suspend = nullptr;
suspend_abi::PushContext push_context = nullptr;
suspend_abi::Deoptimize deoptimize = nullptr;
std::atomic<uint64_t> suspend_deopt_hits{0};
PendingSuspendDeopt pending_deopt;

bool MarkPending(void* thread) { return pending_deopt.Mark(thread); }
bool IsPending(void* thread) { return pending_deopt.IsPending(thread); }
void CancelPending(void* thread) { pending_deopt.Cancel(thread); }
void ResetPending() { pending_deopt.Reset(); }
uint64_t Hits() { return suspend_deopt_hits.load(std::memory_order_acquire); }

void SuspendProxy(void* thread) {
  original_suspend(thread);
  if (!pending_deopt.Consume(thread)) return;
  suspend_deopt_hits.fetch_add(1, std::memory_order_relaxed);
  const suspend_abi::JValue result{0};
  push_context(thread, result, false, {0}, false, suspend_abi::MethodType::kDefault);
  deoptimize(thread);
}
}  // namespace

bool MarkEvolutionSuspendDeopt(void* thread) { return MarkPending(thread); }
bool EvolutionSuspendDeoptPending(void* thread) { return IsPending(thread); }
void CancelEvolutionSuspendDeopt(void* thread) { CancelPending(thread); }
void ResetEvolutionSuspendDeopt() { ResetPending(); }
uint64_t EvolutionSuspendDeoptHits() { return Hits(); }

bool InstallSuspendDeoptHook(HookEngine& hooks, void* /*implementation*/,
                             std::string* error) {
  auto push = reinterpret_cast<suspend_abi::PushContext>(
      hooks.FindSymbol("libart.so", cas::kPushDeoptimizationContext));
  auto deopt = reinterpret_cast<suspend_abi::Deoptimize>(
      hooks.FindSymbol("libart.so", cas::kDeoptimize));
  void* target = hooks.FindSymbol("libart.so", cas::kTestSuspendFromCode);
  if (!push || !deopt || !target) {
    *error = "Missing named suspend entrypoint or deoptimization capability";
    return false;
  }
  push_context = push;
  deoptimize = deopt;
  return hooks.Install(target, reinterpret_cast<void*>(SuspendProxy),
                       reinterpret_cast<void**>(&original_suspend), error) != nullptr;
}

}  // namespace artdcevm::android13
