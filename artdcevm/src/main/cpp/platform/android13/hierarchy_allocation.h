#pragma once
#include "platform/android13/device_profile.h"
#include "api/hook_engine.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/common/rooted_hierarchy_allocator.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace artdcevm::android13 {
// Persistent roots grow below a stable zero-root anchor, never above a device
// temporary HandleScope. Slots remain valid across vector growth and GC.
class HierarchyRootPool {
 public:
  explicit HierarchyRootPool(void* thread);
  ~HierarchyRootPool();
  void* Add(void* object);
  static void* Get(const void* slot);
  static void Set(void* slot,void* object);
 private:
  HandleRootScope anchor_;
  void* original_link_;
  std::vector<std::unique_ptr<unsigned char[]>> chunks_;
  uint32_t used_=64;
};
using HierarchyClassAllocator=void*(*)(void*,void*,void*,void*,uint16_t);
bool ConfigureHierarchyAllocation(HookEngine&,const DeviceProfile& profile,std::string* error);
class HierarchyBatch {
 public:
  explicit HierarchyBatch(void* driver);
  ~HierarchyBatch();
  bool Prepare(void* holder);
  void* Allocate(void* owner,void* old_class_slot,void* cache_slot,uint16_t index,
                 HierarchyClassAllocator original);
  void* Find(const char* descriptor,void* loader);
  void RefreshCache(void* cache_slot);
  // Called before Run returns and the driver's native DexFile owners die.
  // Failure retires only this allocation scope's unpublished replacement classes.
  void Complete(bool committed);
  static HierarchyBatch* Current();
 private:
  friend void CaptureHierarchyAllocatedClass(const void*);
  struct Target { void* owner; void* klass; void* cache; bool structural; };
  void Fail(const char* message);
  void* driver_;
  HierarchyRootPool roots_;
  HierarchyBatch* previous_;
  std::vector<Target> targets_;
  rooted_hierarchy::Allocator allocator_;
};
// Fixed EnsureInstanceFieldIds(Handle<Class>) boundary within the original
// allocation scope captures linked classes even when a later ID allocation fails.
void CaptureHierarchyAllocatedClass(const void* class_slot);
bool HasHierarchyAllocationContext();
void* AllocateHierarchyWithoutBatch(void* owner,void* old_slot,void* parent_slot,
                                   void* cache_slot,uint16_t index,HierarchyClassAllocator);
// The view borrows ART's existing rooted Handle slot for the duration of Verify.
class HierarchyVerificationView {
 public:
  HierarchyVerificationView(void* class_slot,void* cache_slot);
  ~HierarchyVerificationView();
  static void* Resolve(const char* descriptor,void* loader,bool allow_batch);
 private:
  void* class_slot_;
  HierarchyVerificationView* previous_;
};
uint64_t HierarchyDiagnostics(int index);
}
