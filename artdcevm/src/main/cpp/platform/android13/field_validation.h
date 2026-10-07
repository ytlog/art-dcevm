#pragma once
#include "api/hook_engine.h"

namespace artdcevm::android13 {
bool ConfigureEvolutionFieldValidation(HookEngine& hooks, void* implementation, std::string* error);
bool CheckEvolutionFields(void* definition);
}
