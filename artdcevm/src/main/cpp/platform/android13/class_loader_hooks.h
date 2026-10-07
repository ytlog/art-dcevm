#pragma once

#include "api/hook_engine.h"
#include <string>

namespace artdcevm::android13 {

// Prune obsolete-member registries before the device frees their LinearAlloc.
// The private ClassLoaderData layout stays inside the calibrated implementation.
bool InstallClassLoaderHooks(HookEngine& hooks, void* implementation,
                             std::string* error);

}  // namespace artdcevm::android13
