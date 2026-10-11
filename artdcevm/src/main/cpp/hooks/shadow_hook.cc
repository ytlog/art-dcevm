#include "hooks/shadow_hook.h"
#include "shadowhook.h"
namespace artdcevm::hooks {
bool Initialize(std::string* error) {
  // Repeated calls return the result of the first initialization.
  const int result = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
  if (result) *error = shadowhook_to_errmsg(result);
  return result == 0;
}
void* FindSymbol(const char* library, const char* symbol) {
  void* handle = shadowhook_dlopen(library);
  if (!handle) return nullptr;
  void* address = shadowhook_dlsym_dynsym(handle, symbol);
  shadowhook_dlclose(handle);
  return address;
}
void* Install(void* target, void* replacement, void** original, std::string* error) {
  void* token = shadowhook_hook_sym_addr(target, replacement, original);
  if (!token) *error = shadowhook_to_errmsg(shadowhook_get_errno());
  return token;
}
bool Remove(void* token, std::string* error) {
  const int result = shadowhook_unhook(token);
  if (result) *error = shadowhook_to_errmsg(shadowhook_get_errno());
  return result == 0;
}
}  // namespace artdcevm::hooks
