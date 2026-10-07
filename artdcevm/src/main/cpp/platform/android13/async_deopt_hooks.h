#pragma once

#include "api/hook_engine.h"
#include <string>
#include <cstdint>

namespace artdcevm::android13 {

void SetAsyncDeoptProbe(bool enabled);
uint64_t AsyncDeoptProbeHits();

bool InstallAsyncDeoptHook(HookEngine& hooks, void* implementation,
                           std::string* error);

}  // namespace artdcevm::android13
