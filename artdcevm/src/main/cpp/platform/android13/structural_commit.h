#pragma once
#include "api/hook_engine.h"
#include <string>
namespace artdcevm::android13 {
struct StructuralCommitRoots {
  void* mirror; void* old_instances; void* new_instances; void* old_classes; void* new_classes;
};
bool ConfigureStructuralCommit(HookEngine&,void* source,std::string*);
// Device transaction holds the exclusive mutator lock and has completed all
// fallible allocation/verification. Native vectors never cross the ART ABI.
void CommitEvolutionStructurally(void* definition,const void* iterator);
}
