#include "platform/android13/async_deopt_hooks.h"

#include <cstdint>
#include <dlfcn.h>
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
using IsAsyncDeoptimizeable = bool (*)(const void*, uintptr_t);
IsAsyncDeoptimizeable original_is_async = nullptr;
IsAsyncDeoptimizeable allow_debuggable_aot = nullptr;

bool IsAsyncDeoptimizeableProxy(const void* runtime, uintptr_t pc) {
  if (original_is_async(runtime, pc)) return true;
  return allow_debuggable_aot(runtime, pc);
}
}  // namespace

bool InstallAsyncDeoptHook(HookEngine& hooks, void* implementation,
                           std::string* error) {
  auto allow = reinterpret_cast<IsAsyncDeoptimizeable>(
      dlsym(implementation, "ArtDcevmSourceAllowDebuggableAotDeopt"));
  void* target = hooks.FindSymbol("libart.so", cas::kIsAsyncDeoptimizeable);
  if (!allow || !target) {
    *error = "Missing named async-deoptimization symbol or safety predicate";
    return false;
  }
  allow_debuggable_aot = allow;
  return hooks.Install(target, reinterpret_cast<void*>(IsAsyncDeoptimizeableProxy),
                       reinterpret_cast<void**>(&original_is_async), error) != nullptr;
}

}  // namespace artdcevm::android13
