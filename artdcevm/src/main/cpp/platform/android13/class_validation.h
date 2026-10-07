#pragma once
#include "api/hook_engine.h"
namespace artdcevm::android13 {
bool ConfigureEvolutionClassValidation(HookEngine&, void* implementation, std::string* error);
bool CheckEvolutionClass(void* definition);
}
