#pragma once
#include "api/hook_engine.h"
namespace artdcevm::android13 {
bool InstallRetiredFieldHooks(HookEngine& hooks, void* library, std::string* error);
}
