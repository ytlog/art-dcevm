#include "hooks/shadow_hook_engine.h"
#include "shadowhook.h"
#include "support/declared_symbol_lookup.h"
namespace artdcevm {
bool ShadowHookEngine::Initialize(std::string* error) {
  int result = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
  if (result) *error = shadowhook_to_errmsg(result);
  return result == 0;
}
void* ShadowHookEngine::FindSymbol(const char* library, const char* symbol) {
  void* handle = shadowhook_dlopen(library);
  if (!handle) return nullptr;
  void* address = shadowhook_dlsym_dynsym(handle, symbol);
  // One official xDL handle/cache/decompressor owns both exact and uniquely
  // declared clone lookup. Runtime performs no independent ELF/XZ inventory.
  if (!address) address = shadowhook_dlsym_declared_symbol_symtab(handle, symbol);
  shadowhook_dlclose(handle);
  return address;
}
void* ShadowHookEngine::Install(void* target, void* replacement, void** original, std::string* error) {
  void* token = shadowhook_hook_sym_addr(target, replacement, original);
  if (!token) *error = shadowhook_to_errmsg(shadowhook_get_errno());
  return token;
}
bool ShadowHookEngine::Remove(void* token, std::string* error) {
  int result = shadowhook_unhook(token);
  if (result) *error = shadowhook_to_errmsg(shadowhook_get_errno());
  return result == 0;
}
}
