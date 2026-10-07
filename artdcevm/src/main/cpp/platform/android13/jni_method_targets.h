#pragma once

#include "api/hook_engine.h"
#include <string>

namespace artdcevm::android13 {
bool ConfigureJniMethodTargets(HookEngine& hooks, void* implementation, std::string* error);
}
