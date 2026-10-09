#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace artdcevm::source_commit {
// Managed objects stay rooted by ART's transaction Holder throughout the call.
// These native vectors are identity snapshots, not independently owned GC roots.
struct ClassRootOrigin {void* array=nullptr;size_t index=0;};
struct Plan {
  void* mirror=nullptr;
  void* new_classes_root=nullptr;
  std::vector<void*> old_classes,new_classes,old_instances,new_instances;
  bool abstract_instances=false;
  // Each replacement remains rooted in its actual ART Holder partition. Empty
  // retains the original single new_classes_root array and class index.
  std::vector<ClassRootOrigin> class_root_origins{};
};
// Version providers supply source-declared, behaviorally calibrated services.
// Run executes only under ART's final exclusive mutator/disabled-GC transaction
// boundary. It never obtains those locks or establishes this capability itself.
class Services {
 public:
  virtual ~Services()=default;
  virtual bool Validate(const Plan&)const=0;
  // Called exactly once after every rejectable check succeeds, before the
  // first status/field mutation. Publish prepared retirement here; no JNI,
  // suspension or further rejectable validation is allowed.
  virtual void BeginCommit(const Plan&)const {}
  virtual uint32_t Status(void* klass)const=0;
  virtual void SetStatus(void* klass,uint32_t status)const=0;
  virtual void CopyFields(bool is_static,void* target,void* target_class,
                          void* source,void* source_class)const=0;
  virtual void* ObjectClass(void* object)const=0;
  virtual void MarkClassAndMethodsObsolete(void* old_class,void* new_class)const=0;
  virtual void UpdateReflectiveReferences(const Plan&)const=0;
  virtual void ForceDeoptimizeFrames()const=0;
  virtual void ReplaceObjectReferences()const=0;
  // Fixed modern ART replaces native declaring-class GC roots as part of the
  // heap replacement. Restore old member owners before retaining old classes.
  // Earlier providers must independently establish whether their native
  // replacement already preserves those roots; this default is not a proof.
  virtual void RestoreDeclaringClasses(const Plan&)const {}
  // ReplaceReferences rewrites Holder roots, so resolve the replacement again.
  virtual void* NewClassAfterReplacement(void* rooted_array,size_t index)const=0;
  virtual void RetainOldClass(void* new_class,void* old_class)const=0;
  virtual void InvalidateCompiledCode()const=0;
  virtual void DeoptimizeEverythingForAbstractTransition()const=0;
  virtual void ClearInterpreterCaches()const=0;
};
// Failure is returned only before mutation. Native service failures after commit
// begins remain fatal invariants owned by the provider, exactly as in ART Run.
struct CommitObserver {
 void* owner=nullptr;
 void (*begin)(const Plan&,void*) noexcept=nullptr;
};
// Observer consumes the already validated exact plan immediately before the
// provider publishes retirement/permission state. It must not revalidate,
// allocate, suspend or reject. Default preserves the existing source path.
bool Run(const Plan&,const Services&,CommitObserver={});
}
