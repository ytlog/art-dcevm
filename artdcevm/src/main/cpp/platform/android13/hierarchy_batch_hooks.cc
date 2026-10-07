#include "platform/android13/hierarchy_batch_hooks.h"

#include <dlfcn.h>
#include <cstdint>
#include "jvmti.h"
#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13.h"

namespace artdcevm::android13 {
namespace {
using Allocate = bool (*)(void*, void*, void*);
// Forward the pinned arm64 C++ ABI's pointer-sized ObjPtr return and opaque
// Handle arguments without inspecting them. Source-side size checks guard this
// declaration when that implementation is compiled.
using AllocateClass = void* (*)(void*, void*, void*, void*, uint16_t);
using Run = jvmtiError (*)(void*);
using ConfigureOriginals = void (*)(Allocate, Run, AllocateClass);
Allocate original_allocate = nullptr;
AllocateClass original_class_allocate = nullptr;
Run original_run = nullptr;
Allocate evolution_allocate = nullptr;
AllocateClass evolution_class_allocate = nullptr;
Run evolution_run = nullptr;
bool (*has_hierarchy_change)() = nullptr;

bool CallOriginalSubtree(void* definition, void* holder, void* data) {
  return original_allocate(definition, holder, data);
}

jvmtiError CallOriginalRun(void* driver) {
  return original_run(driver);
}

void* CallOriginalClassAllocate(void* definition, void* old_class,
                                void* super_class, void* cache, uint16_t index) {
  return original_class_allocate(definition, old_class, super_class, cache, index);
}

bool AllocateProxy(void* definition, void* holder, void* data) {
  if (ValidationHooks::InSourceTransaction() && has_hierarchy_change())
    return evolution_allocate(definition, holder, data);
  return original_allocate(definition, holder, data);
}

void* AllocateClassProxy(void* definition, void* old_class, void* super_class,
                         void* cache, uint16_t index) {
  if (ValidationHooks::InSourceTransaction() && has_hierarchy_change())
    return evolution_class_allocate(definition, old_class, super_class, cache, index);
  return original_class_allocate(definition, old_class, super_class, cache, index);
}

jvmtiError RunProxy(void* driver) {
  return ValidationHooks::InSourceTransaction() ? evolution_run(driver)
                                                 : original_run(driver);
}
}  // namespace

bool InstallHierarchyBatchHooks(HookEngine& hooks, void* implementation,
                                std::string* error) {
  auto configure = reinterpret_cast<ConfigureOriginals>(
      dlsym(implementation, "ArtDcevmSourceConfigureHierarchyOriginals"));
  auto allocate = reinterpret_cast<Allocate>(
      dlsym(implementation, "ArtDcevmSourceHierarchyAllocateSubtree"));
  auto run = reinterpret_cast<Run>(
      dlsym(implementation, "ArtDcevmSourceHierarchyRun"));
  auto changed = reinterpret_cast<bool (*)()>(
      dlsym(implementation, "ArtDcevmSourceHasHierarchyChange"));
  auto allocate_class = reinterpret_cast<AllocateClass>(
      dlsym(implementation, "ArtDcevmSourceHierarchyAllocator"));
  if (!configure || !allocate || !run || !changed || !allocate_class) {
    *error = "Hierarchy batch implementation exports are incomplete";
    return false;
  }
  void* allocate_target = hooks.FindSymbol("libopenjdkjvmti.so", cas::kAllocateSubtree);
  void* run_target = hooks.FindSymbol("libopenjdkjvmti.so", cas::kRedefineRun);
  void* class_target = hooks.FindSymbol("libopenjdkjvmti.so", cas::kAllocateClass);
  if (!allocate_target || !run_target || !class_target) {
    *error = "Missing named hierarchy batch symbol";
    return false;
  }
  evolution_allocate = allocate;
  evolution_class_allocate = allocate_class;
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
  void* class_token = hooks.Install(class_target, reinterpret_cast<void*>(AllocateClassProxy),
      reinterpret_cast<void**>(&original_class_allocate), error);
  if (!class_token) {
    std::string rollback;
    if (!hooks.Remove(run_token, &rollback)) *error += "; run unhook failed: " + rollback;
    rollback.clear();
    if (!hooks.Remove(allocate_token, &rollback)) *error += "; allocation unhook failed: " + rollback;
    return false;
  }
  // Source code calls back into this library; all device trampolines remain
  // private here and are ready before any source transaction is enabled.
  configure(CallOriginalSubtree, CallOriginalRun, CallOriginalClassAllocate);
  return true;
}

}  // namespace artdcevm::android13
