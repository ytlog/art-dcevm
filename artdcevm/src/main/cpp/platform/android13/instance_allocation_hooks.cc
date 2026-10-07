#include "platform/android13/instance_allocation_hooks.h"

#include <dlfcn.h>
#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
using CollectInstances = bool (*)(void*, void*);
CollectInstances original_collect = nullptr;
CollectInstances evolution_collect = nullptr;

bool CollectProxy(void* definition, void* data) {
  return ValidationHooks::InSourceTransaction() ? evolution_collect(definition, data)
                                                 : original_collect(definition, data);
}
}  // namespace

bool InstallInstanceAllocationHook(HookEngine& hooks, void* implementation,
                                   std::string* error) {
  using ConfigureOriginal = void** (*)();
  auto configure = reinterpret_cast<ConfigureOriginal>(
      dlsym(implementation, "ArtDcevmSourceConfigureInstanceAllocation"));
  auto collect = reinterpret_cast<CollectInstances>(
      dlsym(implementation, "ArtDcevmSourceInstanceAllocation"));
  void* target = hooks.FindSymbol("libopenjdkjvmti.so", cas::kCollectNewInstances);
  if (!configure || !collect || !target) {
    *error = "Missing named instance-allocation symbol or evolution implementation";
    return false;
  }
  void** source_original = configure();
  if (!source_original) {
    *error = "Missing instance-allocation original slot";
    return false;
  }
  evolution_collect = collect;
  void* token = hooks.Install(target, reinterpret_cast<void*>(CollectProxy),
                              reinterpret_cast<void**>(&original_collect), error);
  if (!token) return false;
  // The source implementation calls the device trampoline while preparing a
  // structural transaction. Publish it before any source transaction begins.
  *source_original = reinterpret_cast<void*>(original_collect);
  return true;
}

}  // namespace artdcevm::android13
