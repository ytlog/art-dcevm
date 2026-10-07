#include "platform/android13/suspend_deopt_hooks.h"
#include "platform/android13/suspend_deopt_art_abi.h"
#include "platform/symbols/android13.h"

#include <cassert>
#include <cstring>
#include <dlfcn.h>

using namespace artdcevm::android13;
namespace {
int original_calls = 0;
int finish_calls = 0;
int context_calls = 0;
void PushContext(void* thread, const suspend_abi::JValue& result, bool reference,
                 suspend_abi::ThrowablePtr exception, bool from_code,
                 suspend_abi::MethodType method_type) {
  assert(!EvolutionSuspendDeoptPending(thread));
  assert(result.bits == 0 && !reference && exception.reference == 0 && !from_code);
  assert(method_type == suspend_abi::MethodType::kDefault);
  assert(original_calls > context_calls);
  ++context_calls;
}
void Deoptimize(void*) {
  assert(context_calls == finish_calls + 1);
  ++finish_calls;
}
void Original(void*) { ++original_calls; }

class FakeHooks final : public artdcevm::HookEngine {
 public:
  bool missing = false;
  void (*proxy)(void*) = nullptr;
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* library, const char* symbol) override {
    assert(std::strcmp(library, "libart.so") == 0);
    if (missing) return nullptr;
    if (std::strcmp(symbol, cas::kPushDeoptimizationContext) == 0)
      return reinterpret_cast<void*>(PushContext);
    if (std::strcmp(symbol, cas::kDeoptimize) == 0)
      return reinterpret_cast<void*>(Deoptimize);
    assert(std::strcmp(symbol, cas::kTestSuspendFromCode) == 0);
    return reinterpret_cast<void*>(Original);
  }
  void* Install(void* target, void* replacement, void** original, std::string*) override {
    *original = target;
    proxy = reinterpret_cast<void (*)(void*)>(replacement);
    return this;
  }
  bool Remove(void*, std::string*) override { return true; }
};
}

int main() {
  void* implementation = dlopen(nullptr, RTLD_NOW);
  assert(implementation);
  FakeHooks hooks;
  std::string error;
  hooks.missing = true;
  assert(!InstallSuspendDeoptHook(hooks, implementation, &error));
  assert(!hooks.proxy);
  hooks.missing = false;
  assert(InstallSuspendDeoptHook(hooks, implementation, &error));
  int target = 0, other = 0;
  hooks.proxy(&target);
  assert(original_calls == 1 && finish_calls == 0);
  assert(MarkEvolutionSuspendDeopt(&target));
  hooks.proxy(&other);
  assert(original_calls == 2 && finish_calls == 0 && EvolutionSuspendDeoptPending(&target));
  hooks.proxy(&target);
  assert(original_calls == 3 && finish_calls == 1);
  hooks.proxy(&target);
  assert(original_calls == 4 && finish_calls == 1);
  assert(MarkEvolutionSuspendDeopt(&target));
  CancelEvolutionSuspendDeopt(&other);
  assert(EvolutionSuspendDeoptPending(&target));
  CancelEvolutionSuspendDeopt(&target);
  hooks.proxy(&target);
  assert(finish_calls == 1);
  assert(MarkEvolutionSuspendDeopt(&target));
  ResetEvolutionSuspendDeopt();
  hooks.proxy(&target);
  assert(finish_calls == 1);
  assert(context_calls == 1 && EvolutionSuspendDeoptHits() == 1);
  dlclose(implementation);
}
