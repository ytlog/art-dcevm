#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Loads genuine compiler/linker ELF output; never executes its ARM64 bytes. Returns 0 on success, -1 on failure. */
int native_upcall_elf_load(const char* path);
void native_upcall_elf_unload(void);
void* native_upcall_elf_entry(const char* exact_name);
size_t native_upcall_elf_size(const char* exact_name);
uintptr_t native_upcall_elf_return_pc(const char* exact_name);
/* Changes the loaded ELF metadata copy, preserving the linked input file. */
int native_upcall_elf_corrupt(const char* name, const char* mutation);
#ifdef __cplusplus
}
#endif
