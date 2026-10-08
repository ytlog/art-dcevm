#include "hooks/shadow_hook_engine.h"
#include "shadowhook.h"
#include "support/elf_modules.h"
#include "support/elf_symbol_names.h"
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
  if (!address) address = shadowhook_dlsym_symtab(handle, symbol);
  if (!address) {
    // Clang unique internal linkage retains the fixed declaration's mangled
    // name with a numeric suffix. Resolve its complete ELF name through the
    // same ShadowHook handle; never use ELF st_value to manufacture an address.
    const auto module = FindLoadedModule(library);
    if (!module.path.empty()) {
      std::string unique;
      {
        std::lock_guard<std::mutex> lock(symbol_names_mutex_);
        const auto key = module.path;
        auto cached = symbol_names_.find(key);
        if (cached == symbol_names_.end())
          cached = symbol_names_.emplace(key, ReadUniqueElfSymbolNames(module.path)).first;
        auto found = cached->second.find(symbol);
        if (found != cached->second.end()) unique = found->second;
      }
      if (!unique.empty()) address = shadowhook_dlsym_symtab(handle, unique.c_str());
    }
  }
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
