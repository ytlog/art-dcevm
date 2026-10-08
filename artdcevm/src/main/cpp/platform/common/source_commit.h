#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace artdcevm::source_commit {
// Managed objects stay rooted by ART's transaction Holder throughout the call.
// These native vectors are identity snapshots, not independently owned GC roots.
struct Plan {
  void* mirror=nullptr;
  void* new_classes_root=nullptr;
  std::vector<void*> old_classes,new_classes,old_instances,new_instances;
  bool abstract_instances=false;
};
// Version providers supply source-declared, behaviorally calibrated services.
// Run executes only under ART's final exclusive mutator/disabled-GC transaction
// boundary. It never obtains those locks or establishes this capability itself.
class Services {
 public:
  virtual ~Services()=default;
  virtual bool Validate(const Plan&)const=0;
  virtual uint32_t Status(void* klass)const=0;
  virtual void SetStatus(void* klass,uint32_t status)const=0;
  virtual void CopyFields(bool is_static,void* target,void* target_class,
                          void* source,void* source_class)const=0;
  virtual void* ObjectClass(void* object)const=0;
  virtual void MarkClassAndMethodsObsolete(void* old_class,void* new_class)const=0;
  virtual void UpdateReflectiveReferences(const Plan&)const=0;
  virtual void ForceDeoptimizeFrames()const=0;
  virtual void ReplaceObjectReferences()const=0;
  // ReplaceReferences rewrites Holder roots, so resolve the replacement again.
  virtual void* NewClassAfterReplacement(void* rooted_array,size_t index)const=0;
  virtual void RetainOldClass(void* new_class,void* old_class)const=0;
  virtual void InvalidateCompiledCode()const=0;
  virtual void DeoptimizeEverythingForAbstractTransition()const=0;
  virtual void ClearInterpreterCaches()const=0;
};
// Failure is returned only before mutation. Native service failures after commit
// begins remain fatal invariants owned by the provider, exactly as in ART Run.
bool Run(const Plan&,const Services&);
}
