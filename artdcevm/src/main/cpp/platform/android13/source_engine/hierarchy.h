#pragma once
#include "base/locks.h"
#include "handle.h"
#include "handle_scope.h"
#include "jvmti.h"
namespace art { class DexFile; namespace mirror { class Class; class ClassLoader; class DexCache; } }
namespace artdcevm::android13::source {
class HierarchyTransactionScope {
 public:
  HierarchyTransactionScope();
  ~HierarchyTransactionScope();
 private:
  uint32_t previous_;
};
bool HasHierarchyChange();
bool HasHierarchyNarrowing();
}
