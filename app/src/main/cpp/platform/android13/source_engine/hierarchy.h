#pragma once
#include "base/locks.h"
#include "handle.h"
#include "handle_scope.h"
#include "jvmti.h"
#include <functional>
#include <string>
#include <vector>
namespace art { class DexFile; namespace mirror { class Class; class ClassLoader; class DexCache; } }
namespace artdcevm::android13::source {
class HierarchyTransactionScope {
 public:
  HierarchyTransactionScope();
  ~HierarchyTransactionScope();
 private:
  bool previous_;
  bool previous_narrowed_;
};
bool HasHierarchyChange();
bool HasHierarchyNarrowing();
// Allocation remains in the device ART. This scope remembers unpublished
// replacements so a requested parent is allocated once, before its new child.
class HierarchyAllocationScope {
 public:
  HierarchyAllocationScope(art::Thread* self, std::function<void(const char*)> fail);
  ~HierarchyAllocationScope();
  static HierarchyAllocationScope* Current();
  bool Prepared() const { return !targets_.empty(); }
  art::ObjPtr<art::mirror::Class> Find(const char* name, art::ObjPtr<art::mirror::ClassLoader> loader);
  void RefreshVerificationCache(art::Handle<art::mirror::DexCache> cache);
  void Add(void* owner, art::ObjPtr<art::mirror::Class> old_class,
           art::ObjPtr<art::mirror::DexCache> cache, bool structural);
  art::ObjPtr<art::mirror::Class> Allocate(void* owner,
      art::Handle<art::mirror::Class> old_class,
      art::Handle<art::mirror::DexCache> cache, uint16_t index);
 private:
  struct Target {
    void* owner;
    art::Handle<art::mirror::Class> old_class;
    art::Handle<art::mirror::DexCache> cache;
    bool structural;
  };
  struct Replacement {
    art::Handle<art::mirror::Class> old_class;
    art::MutableHandle<art::mirror::Class> new_class;
  };
  art::VariableSizedHandleScope roots_;
  std::function<void(const char*)> fail_;
  std::vector<Target> targets_;
  std::vector<Replacement> replacements_;
  HierarchyAllocationScope* previous_;
};
// Removing a superclass or interface requires the precommit receiver guard
// and deoptimization.
jvmtiError CheckHierarchyAdditions(art::Handle<art::mirror::Class> current,
    const art::DexFile& dex, art::MutableHandle<art::mirror::Class>* desired_super,
    bool* changed, std::string* error) REQUIRES_SHARED(art::Locks::mutator_lock_);
}
