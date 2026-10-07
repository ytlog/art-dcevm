#pragma once

#include "api/hook_engine.h"
#include <cstddef>
#include <string>

namespace artdcevm::android13 {

bool ConfigureRetiredMethodRegistry(HookEngine& hooks, void* implementation,
                                    std::string* error);
bool IsRetiredMethodPointer(void* method);
size_t RetiredMethodCount();
size_t PruneRetiredMethods(const void* allocator) noexcept;

}  // namespace artdcevm::android13
