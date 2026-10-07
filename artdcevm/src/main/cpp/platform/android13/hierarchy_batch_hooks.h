#pragma once

#include "api/hook_engine.h"
#include <string>

namespace artdcevm::android13 {

// The enhancement library owns the named device hooks and dispatches each
// call to the device original or the active evolution implementation.
bool InstallHierarchyBatchHooks(HookEngine& hooks, void* implementation,
                                std::string* error);

}  // namespace artdcevm::android13
