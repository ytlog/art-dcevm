#include "platform/common/source_commit.h"
#include <cstdlib>
namespace artdcevm::source_commit {
bool Run(const Plan& plan,const Services& services){
  if(!plan.mirror||!plan.new_classes_root||plan.old_classes.empty()||
     plan.old_classes.size()!=plan.new_classes.size()||
     plan.old_instances.size()!=plan.new_instances.size()||!services.Validate(plan))return false;
  for(size_t i=0;i<plan.old_classes.size();++i){
    auto* old=plan.old_classes[i];auto* replacement=plan.new_classes[i];
    const uint32_t status=services.Status(old);
    if(status>services.Status(replacement))services.SetStatus(replacement,status);
    services.CopyFields(true,replacement,replacement,old,old);
  }
  for(size_t i=0;i<plan.old_instances.size();++i){
    auto* old=plan.old_instances[i];auto* replacement=plan.new_instances[i];
    services.CopyFields(false,replacement,services.ObjectClass(replacement),old,services.ObjectClass(old));
  }
  for(size_t i=0;i<plan.old_classes.size();++i)
    services.MarkClassAndMethodsObsolete(plan.old_classes[i],plan.new_classes[i]);
  services.UpdateReflectiveReferences(plan);
  services.ForceDeoptimizeFrames();
  services.ReplaceObjectReferences();
  services.RestoreDeclaringClasses(plan);
  for(size_t i=0;i<plan.old_classes.size();++i){
    auto* replacement=services.NewClassAfterReplacement(plan.new_classes_root,i);
    if(!replacement)std::abort();
    services.RetainOldClass(replacement,plan.old_classes[i]);
  }
  services.InvalidateCompiledCode();
  if(plan.abstract_instances)services.DeoptimizeEverythingForAbstractTransition();
  services.ClearInterpreterCaches();
  return true;
}
}
