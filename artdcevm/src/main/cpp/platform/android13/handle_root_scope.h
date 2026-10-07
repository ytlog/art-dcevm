#pragma once
#include "api/hook_engine.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
namespace artdcevm::android13 {
bool ConfigureHandleRootScopes(HookEngine&, void* implementation, std::string* error);
void* CurrentHierarchyThread();
void ClearHierarchyException(void* thread);
// ART's HandleScope root walker sees this fixed header and its compressed slots.
// The caller is the current runnable thread with the mutator lock; no pointer
// obtained from Get may survive a possible suspension/GC. Slots stay stable.
class HandleRootScope {
 public:
  HandleRootScope(void* thread, uint32_t count);
  ~HandleRootScope();
  HandleRootScope(const HandleRootScope&) = delete;
  HandleRootScope& operator=(const HandleRootScope&) = delete;
  void* Slot(size_t index);
  void* Get(size_t index);
  void Set(size_t index, void* object);
  void* NativeScope() { return storage_.get(); }
 private:
  void* thread_;
  uint32_t count_;
  std::unique_ptr<unsigned char[]> storage_;
};
}
