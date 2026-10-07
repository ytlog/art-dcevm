#pragma once
#define SHADOWHOOK_MODE_UNIQUE 1
int shadowhook_init(int, bool);
const char* shadowhook_to_errmsg(int);
void* shadowhook_dlopen(const char*);
void* shadowhook_dlsym_dynsym(void*, const char*);
void* shadowhook_dlsym_symtab(void*, const char*);
void shadowhook_dlclose(void*);
void* shadowhook_hook_sym_addr(void*, void*, void**);
int shadowhook_get_errno();
int shadowhook_unhook(void*);
