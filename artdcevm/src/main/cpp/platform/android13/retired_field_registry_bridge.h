#pragma once

#include "api/hook_engine.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace artdcevm::android13 {

bool ConfigureRetiredFieldRegistry(HookEngine& hooks, void* implementation,
                                   std::string* error);
size_t PruneRetiredFields(const void* allocator) noexcept;
size_t RetiredFieldSlotCount();
bool IsRetiredFieldPointer(void* field);
void* ResolveRetiredFieldToken(uint32_t token);

}  // namespace artdcevm::android13
