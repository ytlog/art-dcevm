#pragma once
#include "platform/android13/structural_member_mapping.h"
#include "platform/android13/structural_reflective_abi.h"
namespace artdcevm::android13 {
bool ConfigureStructuralReflectiveTargets(HookEngine&,void* implementation,std::string*);
structural_reflective_abi::SourceView ReadStructuralReflectionSource(const structural_reflective_abi::SourceInfo&);
// No managed allocation or suspension; all arguments remain rooted by ART's exclusive transaction.
void VisitStructuralReflectiveTargets(void* runtime,void* mirror_class,
 const std::vector<void*>& old_classes,const std::vector<void*>& new_classes,
 const StructuralMemberMappings&,bool hierarchy_changed);
}
