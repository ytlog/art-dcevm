#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Official ShadowHook xDL overlay. Exact-first, strictly unique numeric clone
// names only; address resolution remains in the official xDL implementation.
void* shadowhook_dlsym_declared_symbol_symtab(void* handle,const char* declaration);
#ifdef __cplusplus
}
#endif
