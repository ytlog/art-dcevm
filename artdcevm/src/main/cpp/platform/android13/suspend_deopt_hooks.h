#pragma once

#include "api/hook_engine.h"
#include "platform/android13/pending_suspend_deopt.h"
#include <string>
#include <cstdint>

namespace artdcevm::android13 {

bool MarkEvolutionSuspendDeopt(void* thread,uint64_t* token=nullptr);
bool EvolutionSuspendDeoptPending(void* thread,uint64_t token=0);
SuspendDeoptState EvolutionSuspendDeoptState(void* thread,uint64_t token=0);
bool EvolutionSuspendDeoptInFlight();
bool ReleaseEvolutionSuspendDeopt(void* thread,uint64_t token=0);
// Called at the existing interpreter-deopt Hook, before interpreting frames.
void AcknowledgeEvolutionSuspendDeopt(void* thread, void* shadow_frame, bool from_code);
void CancelEvolutionSuspendDeopt(void* thread,uint64_t token=0);
void ResetEvolutionSuspendDeopt();
uint64_t EvolutionSuspendDeoptHits();

bool InstallSuspendDeoptHook(HookEngine& hooks, std::string* error);

}  // namespace artdcevm::android13
