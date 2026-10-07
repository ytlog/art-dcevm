#include "platform/android13/suspend_deopt_hooks.h"

#include <dlfcn.h>
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
using SuspendFromCode = void (*)(void*);
SuspendFromCode original_suspend = nullptr;
SuspendFromCode finish_pending_deopt = nullptr;

void SuspendProxy(void* thread) {
  original_suspend(thread);
  finish_pending_deopt(thread);
}
}  // namespace

bool InstallSuspendDeoptHook(HookEngine& hooks, void* implementation,
                             std::string* error) {
  auto finish = reinterpret_cast<SuspendFromCode>(
      dlsym(implementation, "ArtDcevmSourceFinishPendingSuspendDeopt"));
  void* target = hooks.FindSymbol("libart.so", cas::kTestSuspendFromCode);
  if (!finish || !target) {
    *error = "Missing named suspend entrypoint or pending-deopt handler";
    return false;
  }
  finish_pending_deopt = finish;
  return hooks.Install(target, reinterpret_cast<void*>(SuspendProxy),
                       reinterpret_cast<void**>(&original_suspend), error) != nullptr;
}

}  // namespace artdcevm::android13
