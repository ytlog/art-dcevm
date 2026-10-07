#pragma once

#include "api/hook_engine.h"
#include "platform/android13/allocator_membership.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace artdcevm::android13 {

bool ConfigureRetiredFieldRegistry(AllocatorContains contains, std::string* error);
size_t PruneRetiredFields(const void* allocator) noexcept;
size_t RetiredFieldSlotCount();
void RetireFieldPointer(void*) noexcept;
uint32_t RetiredFieldTokenFor(void*);
bool IsRetiredFieldPointer(void* field);
void* ResolveRetiredFieldToken(uint32_t token);

}  // namespace artdcevm::android13
