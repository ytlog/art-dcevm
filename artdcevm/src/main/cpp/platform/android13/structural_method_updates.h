#pragma once
#include "api/hook_engine.h"
#include <string>
namespace artdcevm::android13 {
bool ConfigureStructuralMethodUpdates(HookEngine&, void* implementation, std::string*);
// Called exclusively under ART's structural transaction mutator lock; never suspends.
void UpdateStructuralMethods(void* old_class, void* new_class);
// Shared exact declaration identity: name/signature and staticness.
void* FindStructuralMethodReplacement(void* old_method, void* new_class);
}
