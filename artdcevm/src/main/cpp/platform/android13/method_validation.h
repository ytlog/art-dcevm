#pragma once
#include "api/hook_engine.h"
namespace artdcevm::android13 {
bool ConfigureEvolutionMethodValidation(HookEngine&, void* implementation, std::string* error);
bool CheckEvolutionMethods(void* definition);
}
