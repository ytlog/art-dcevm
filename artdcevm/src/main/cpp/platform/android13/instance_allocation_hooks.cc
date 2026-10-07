#include "platform/android13/instance_allocation_hooks.h"

#include <dlfcn.h>
#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
using CollectInstances = bool (*)(void*, void*);
CollectInstances original_collect = nullptr;
CollectInstances evolution_collect = nullptr;

bool CallOriginalCollect(void* definition, void* data) {
  return original_collect(definition, data);
}

bool CollectProxy(void* definition, void* data) {
  return ValidationHooks::InSourceTransaction() ? evolution_collect(definition, data)
                                                 : original_collect(definition, data);
}
}  // namespace

bool InstallInstanceAllocationHook(HookEngine& hooks, void* implementation,
                                   std::string* error) {
  using ConfigureOriginal = void (*)(CollectInstances);
  auto configure = reinterpret_cast<ConfigureOriginal>(
      dlsym(implementation, "ArtDcevmSourceConfigureInstanceAllocation"));
  auto collect = reinterpret_cast<CollectInstances>(
      dlsym(implementation, "ArtDcevmSourceInstanceAllocation"));
  void* target = hooks.FindSymbol("libopenjdkjvmti.so", cas::kCollectNewInstances);
  if (!configure || !collect || !target) {
    *error = "Missing named instance-allocation symbol or evolution implementation";
    return false;
  }
  evolution_collect = collect;
  void* token = hooks.Install(target, reinterpret_cast<void*>(CollectProxy),
                              reinterpret_cast<void**>(&original_collect), error);
  if (!token) return false;
  // The source implementation calls back here; the device trampoline remains
  // private to the enhancement library.
  configure(CallOriginalCollect);
  return true;
}

}  // namespace artdcevm::android13
