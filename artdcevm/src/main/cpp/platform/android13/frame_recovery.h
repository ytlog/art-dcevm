#pragma once
#include "platform/android13/stack_access.h"
namespace artdcevm::android13 {
bool ConfigureFrameRecovery(HookEngine&,const DeviceProfile&,std::string*);
// Call only on a stable stack under the mutator lock. No allocation, suspension
// or managed register access. Counts are checked for each inlinee and its carrier.
// Native ownership and AOT debuggable policy are checked before the device
// decoder reads the vendor header. physical_method is the machine-frame carrier.
bool ReadEvolutionCodeInfo(void* physical_method,const void* header,uintptr_t pc,stack_abi::CodeInfo*);
bool IsEvolutionFrameRecoverable(const stack_abi::Visitor&);
bool IsEvolutionPcRecoverable(void* thread,uintptr_t pc);
}
