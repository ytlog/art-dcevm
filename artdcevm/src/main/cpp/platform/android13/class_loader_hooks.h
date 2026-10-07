#pragma once

#include "api/hook_engine.h"
#include <cstdint>
#include <string>

namespace artdcevm::android13 {

// Prune obsolete-member registries before the device frees their LinearAlloc.
bool InstallClassLoaderHooks(HookEngine& hooks, std::string* error);
uint64_t ClassLoaderDeleteCalls();
uint64_t RetiredPrunedCount(int kind);

}  // namespace artdcevm::android13
