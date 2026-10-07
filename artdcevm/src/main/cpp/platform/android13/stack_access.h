#pragma once
#include "api/hook_engine.h"
#include "platform/android13/stack_access_abi.h"
#include <string>
namespace artdcevm::android13 {
bool ConfigureStackAccess(HookEngine& hooks, void* source, std::string* error);
}
