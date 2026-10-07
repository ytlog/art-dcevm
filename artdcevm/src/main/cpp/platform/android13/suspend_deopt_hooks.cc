#include "platform/android13/suspend_deopt_hooks.h"
#include "platform/android13/pending_suspend_deopt.h"
#include "platform/android13/pending_suspend_deopt_abi.h"

#include <dlfcn.h>
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
using SuspendFromCode = void (*)(void*);
SuspendFromCode original_suspend = nullptr;
SuspendFromCode finish_pending_deopt = nullptr;
PendingSuspendDeopt pending_deopt;

bool MarkPending(void* thread) { return pending_deopt.Mark(thread); }
bool IsPending(void* thread) { return pending_deopt.IsPending(thread); }
void CancelPending(void* thread) { pending_deopt.Cancel(thread); }
void ResetPending() { pending_deopt.Reset(); }

void SuspendProxy(void* thread) {
  original_suspend(thread);
  if (pending_deopt.Consume(thread)) finish_pending_deopt(thread);
}
}  // namespace

bool InstallSuspendDeoptHook(HookEngine& hooks, void* implementation,
                             std::string* error) {
  auto configure = reinterpret_cast<ConfigurePendingSuspendDeopt>(
      dlsym(implementation, "ArtDcevmSourceConfigureSuspendDeopt"));
  auto finish = reinterpret_cast<SuspendFromCode>(
      dlsym(implementation, "ArtDcevmSourceFinishPendingSuspendDeopt"));
  void* target = hooks.FindSymbol("libart.so", cas::kTestSuspendFromCode);
  if (!configure || !finish || !target) {
    *error = "Missing named suspend entrypoint or pending-deopt callbacks";
    return false;
  }
  configure(MarkPending, IsPending, CancelPending, ResetPending);
  finish_pending_deopt = finish;
  return hooks.Install(target, reinterpret_cast<void*>(SuspendProxy),
                       reinterpret_cast<void**>(&original_suspend), error) != nullptr;
}

}  // namespace artdcevm::android13
