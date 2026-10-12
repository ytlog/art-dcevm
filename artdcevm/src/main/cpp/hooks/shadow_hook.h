#pragma once

#include <string>

#include "shadowhook.h"

// Hook mechanics only. Named ELF lookup belongs to platform/symbols.
namespace artdcevm::hooks {

inline bool Initialize(std::string* error) {
  const int result = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
  if (result != 0) *error = shadowhook_to_errmsg(result);
  return result == 0;
}

inline void* Install(void* target, void* replacement, void** original, std::string* error) {
  void* token = shadowhook_hook_sym_addr(target, replacement, original);
  if (!token) *error = shadowhook_to_errmsg(shadowhook_get_errno());
  return token;
}

inline bool Remove(void* token, std::string* error) {
  const int result = shadowhook_unhook(token);
  if (result != 0) *error = shadowhook_to_errmsg(result);
  return result == 0;
}

}  // namespace artdcevm::hooks
