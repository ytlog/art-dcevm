#pragma once
#include <string>
// Official ShadowHook in unique mode. Lookups only consult .dynsym; every
// catalogued ART symbol lives there.
namespace artdcevm::hooks {
bool Initialize(std::string* error);
void* FindSymbol(const char* library, const char* symbol);
// Returns the stub token, or nullptr with `error` set.
void* Install(void* target, void* replacement, void** original, std::string* error);
bool Remove(void* token, std::string* error);
}  // namespace artdcevm::hooks
