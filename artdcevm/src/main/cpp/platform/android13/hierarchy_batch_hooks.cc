#include "platform/android13/hierarchy_batch_hooks.h"

#include <dlfcn.h>
#include "jvmti.h"
#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
using Allocate = bool (*)(void*, void*, void*);
using Run = jvmtiError (*)(void*);
using ConfigureOriginal = void** (*)(int);
Allocate original_allocate = nullptr;
Run original_run = nullptr;
Allocate evolution_allocate = nullptr;
Run evolution_run = nullptr;
bool (*has_hierarchy_change)() = nullptr;

bool AllocateProxy(void* definition, void* holder, void* data) {
  if (ValidationHooks::InSourceTransaction() && has_hierarchy_change())
    return evolution_allocate(definition, holder, data);
  return original_allocate(definition, holder, data);
}

jvmtiError RunProxy(void* driver) {
  return ValidationHooks::InSourceTransaction() ? evolution_run(driver)
                                                 : original_run(driver);
}
}  // namespace

bool InstallHierarchyBatchHooks(HookEngine& hooks, void* implementation,
                                std::string* error) {
  auto configure = reinterpret_cast<ConfigureOriginal>(
      dlsym(implementation, "ArtDcevmSourceConfigureHierarchyBatch"));
  auto allocate = reinterpret_cast<Allocate>(
      dlsym(implementation, "ArtDcevmSourceHierarchyAllocateSubtree"));
  auto run = reinterpret_cast<Run>(
      dlsym(implementation, "ArtDcevmSourceHierarchyRun"));
  auto changed = reinterpret_cast<bool (*)()>(
      dlsym(implementation, "ArtDcevmSourceHasHierarchyChange"));
  if (!configure || !allocate || !run || !changed) {
    *error = "Hierarchy batch implementation exports are incomplete";
    return false;
  }
  void* allocate_target = hooks.FindSymbol("libopenjdkjvmti.so", cas::kAllocateSubtree);
  void* run_target = hooks.FindSymbol("libopenjdkjvmti.so", cas::kRedefineRun);
  void** source_allocate_original = configure(0);
  void** source_run_original = configure(1);
  if (!allocate_target || !run_target || !source_allocate_original || !source_run_original) {
    *error = "Missing named hierarchy batch symbol or original slot";
    return false;
  }
  evolution_allocate = allocate;
  evolution_run = run;
  has_hierarchy_change = changed;
  void* allocate_token = hooks.Install(allocate_target,
      reinterpret_cast<void*>(AllocateProxy), reinterpret_cast<void**>(&original_allocate), error);
  if (!allocate_token) return false;
  void* run_token = hooks.Install(run_target,
      reinterpret_cast<void*>(RunProxy), reinterpret_cast<void**>(&original_run), error);
  if (!run_token) {
    std::string rollback;
    if (!hooks.Remove(allocate_token, &rollback)) *error += "; unhook failed: " + rollback;
    return false;
  }
  // The source implementation calls these device trampolines from its
  // structural transaction. Publish both before that transaction is enabled.
  *source_allocate_original = reinterpret_cast<void*>(original_allocate);
  *source_run_original = reinterpret_cast<void*>(original_run);
  return true;
}

}  // namespace artdcevm::android13
