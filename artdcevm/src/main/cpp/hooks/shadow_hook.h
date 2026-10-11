#pragma once
#include <string>
// Official ShadowHook in unique mode. Local names are resolved by ShadowHook
// from ELF symbol tables, including .gnu_debugdata; never infer addresses.
namespace artdcevm::hooks {
bool Initialize(std::string* error);
void* FindSymbol(const char* library, const char* symbol, bool include_local = false);
// Returns the stub token, or nullptr with `error` set.
void* Install(void* target, void* replacement, void** original, std::string* error);
bool Remove(void* token, std::string* error);
}  // namespace artdcevm::hooks
