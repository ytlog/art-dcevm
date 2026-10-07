#pragma once
#include "api/hook_engine.h"
namespace artdcevm::android13 {
bool InstallRetiredMethodHooks(HookEngine& hooks, std::string* error);
int RejectedRetiredInvocations();
}
