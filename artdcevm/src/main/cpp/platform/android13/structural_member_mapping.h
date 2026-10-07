#pragma once
#include "api/hook_engine.h"
#include <unordered_map>
#include <vector>
namespace artdcevm::android13 {
struct StructuralMemberMappings {
  std::unordered_map<void*,void*> methods,fields;
};
bool ConfigureStructuralMemberMappings(HookEngine&,std::string*);
StructuralMemberMappings BuildStructuralMemberMappings(const std::vector<void*>& old_classes,
                                                       const std::vector<void*>& new_classes);
bool SameStructuralMethodIdentity(void*,void*);
}
