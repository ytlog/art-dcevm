#pragma once

#include "api/hook_engine.h"
#include <cstdint>
#include <string>

namespace artdcevm::android13 {
bool ConfigureFieldCopyStorage(HookEngine& hooks, std::string* error);
// Non-transactional structural commit access. ART holds the mutator lock,
// suspends mutation, and supplies rooted to-space objects and valid ArtFields.
// Ordinary Java storage uses relaxed atomics; volatile fields use seq_cst.
uint64_t ReadCopyValue(void* object, void* field, char descriptor);
void WriteCopyValue(void* object, void* field, char descriptor, uint64_t value);
void TransferCopyMonitor(void* target, void* source);
void CopyWriteBarrier(void* object);
}
