#pragma once

#include "api/hook_engine.h"
#include <string>
#include <cstdint>

namespace artdcevm::android13 {

bool MarkEvolutionSuspendDeopt(void* thread);
bool EvolutionSuspendDeoptPending(void* thread);
void CancelEvolutionSuspendDeopt(void* thread);
void ResetEvolutionSuspendDeopt();
uint64_t EvolutionSuspendDeoptHits();

bool InstallSuspendDeoptHook(HookEngine& hooks, std::string* error);

}  // namespace artdcevm::android13
