#include "platform/android13/hierarchy_batch_hooks.h"

#include "platform/android13/hierarchy_allocation.h"
#include "platform/abi/arm64/common/hierarchy_allocation.h"
#include "platform/android13/hierarchy_validation.h"
#include <cstdint>
#include "jvmti.h"
#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13/source_symbols.h"

namespace artdcevm::android13 {
namespace {
using Allocate = bool (*)(void*, void*, void*);
// Forward the pinned arm64 C++ ABI's pointer-sized ObjPtr return and opaque
// Handle arguments without inspecting them. Source-side size checks guard this
// declaration when that implementation is compiled.
using AllocateClass = void* (*)(void*, void*, void*, void*, uint16_t);
using Run = jvmtiError (*)(void*);
Allocate original_allocate = nullptr;
AllocateClass original_class_allocate = nullptr;
Run original_run = nullptr;
using EnsureIds=bool (*)(const void*);
EnsureIds original_ensure_ids=nullptr;
bool EnsureIdsProxy(const void* class_slot) {
  CaptureHierarchyAllocatedClass(class_slot);
  return original_ensure_ids(class_slot);
}

bool AllocateProxy(void* definition, void* holder, void* data) {
  if (ValidationHooks::InSourceTransaction() && HasHierarchyChange()) {
    auto* batch = HierarchyBatch::Current();
    if (!batch || !batch->Prepare(holder)) return false;
  }
  return original_allocate(definition, holder, data);
}

void* AllocateClassProxy(void* definition, void* old_class, void* super_class,
                         void* cache, uint16_t index) {
  if (ValidationHooks::InSourceTransaction() && HasHierarchyChange()) {
    auto* batch = HierarchyBatch::Current();
    return batch ? batch->Allocate(definition, old_class, cache, index, original_class_allocate)
                 : AllocateHierarchyWithoutBatch(definition, old_class, super_class, cache,
                                                  index, original_class_allocate);
  }
  return original_class_allocate(definition, old_class, super_class, cache, index);
}

jvmtiError RunProxy(void* driver) {
  if (!ValidationHooks::InSourceTransaction()) return original_run(driver);
  HierarchyBatch batch(driver);
  jvmtiError result=original_run(driver);
  batch.Complete(result==JVMTI_ERROR_NONE);
  return result;
}
}  // namespace

bool InstallHierarchyBatchHooks(HookEngine& hooks, const DeviceProfile& profile,
                                std::string* error) {
  if (!ConfigureHierarchyAllocation(hooks, profile, error)) return false;
  void* allocate_target = hooks.FindSymbol("libopenjdkjvmti.so", abi::arm64::android_common::kFinishSubtreeAllocations);
  void* run_target = hooks.FindSymbol("libopenjdkjvmti.so", ::artdcevm::symbols::android13::kRedefineRun);
  void* class_target = hooks.FindSymbol("libopenjdkjvmti.so", abi::arm64::android_common::kHierarchyAllocator);
  void* ids_target=hooks.FindSymbol("libart.so",abi::arm64::android_common::kCaptureLinkedClass);
  if (!allocate_target || !run_target || !class_target || !ids_target) {
    *error = "Missing named hierarchy batch symbol";
    return false;
  }
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
  if(!hooks.Install(ids_target,reinterpret_cast<void*>(EnsureIdsProxy),
                    reinterpret_cast<void**>(&original_ensure_ids),error)) {
    std::string rollback;
    if(!hooks.Remove(class_token,&rollback))*error+="; class unhook failed: "+rollback;
    rollback.clear();if(!hooks.Remove(run_token,&rollback))*error+="; run unhook failed: "+rollback;
    rollback.clear();if(!hooks.Remove(allocate_token,&rollback))*error+="; allocation unhook failed: "+rollback;
    return false;
  }
  return true;
}

}  // namespace artdcevm::android13
