#pragma once

#include "api/hook_engine.h"
#include <string>

namespace artdcevm::android13 {

bool InstallInstanceAllocationHook(HookEngine& hooks, void* implementation,
                                   std::string* error);

}  // namespace artdcevm::android13
