/* ART-DCEVM local extension of official ShadowHook 2.0.1 xDL.
 * Included at the end of xdl.c; uses the existing loader/cache/decompressor.
 * No canonical-name matching, IFUNC resolution, next-symbol inference or code reads.
 */
static bool sh_metadata_accept(const xdl_t *self, const ElfW(Sym) *sym,
                               const char *strings, size_t strings_size,
                               const char *name, uintptr_t *begin, size_t *size,
                               bool *found) {
  if (sym->st_name >= strings_size) return true;
  const char *candidate = strings + sym->st_name;
  const char *end = memchr(candidate, 0, strings_size - sym->st_name);
  if (NULL == end) return false;
  if (0 != strcmp(candidate, name)) return true;
  if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC || sym->st_size == 0 ||
      !XDL_SYMTAB_IS_EXPORT_SYM(sym->st_shndx) ||
      sym->st_value > UINTPTR_MAX - self->load_bias) return false;
  uintptr_t address = self->load_bias + sym->st_value;
  if (sym->st_size > UINTPTR_MAX - address) return false;
  bool executable = false;
  for (size_t i = 0; i < self->dlpi_phnum; ++i) {
    const ElfW(Phdr) *phdr = self->dlpi_phdr + i;
    if (phdr->p_type != PT_LOAD || (phdr->p_flags & PF_X) == 0 ||
        phdr->p_vaddr > UINTPTR_MAX - self->load_bias) continue;
    uintptr_t segment = self->load_bias + phdr->p_vaddr;
    if (phdr->p_memsz > UINTPTR_MAX - segment) continue;
    if (address >= segment && address + sym->st_size <= segment + phdr->p_memsz)
      executable = true;
  }
  if (!executable || (*found && (*begin != address || *size != sym->st_size))) return false;
  *begin = address;
  *size = sym->st_size;
  *found = true;
  return true;
}

/* Export is declared by the consuming project's named_symbol_extent.h.
 * Existing shadowhook_dlopen handles are exactly xdl_t handles.
 * Called only during preparation, while the loaded module is held alive.
 */
int shadowhook_get_named_function_extent(void *handle, const char *name,
                                        const void *expected, uintptr_t *begin, size_t *size);
int shadowhook_get_named_function_extent(void *handle, const char *name,
                                        const void *expected, uintptr_t *begin, size_t *size) {
  if (NULL == begin || NULL == size) return -1;
  *begin = 0;
  *size = 0;
  if (NULL == handle || NULL == name || name[0] == 0 || NULL == expected) return -1;
  xdl_t *self = handle;
  uintptr_t address = 0;
  size_t extent = 0;
  bool found = false;
  xdl_ensure_dynsym_loaded(self);
  if (NULL != self->dynsym) {
    ElfW(Sym) *sym = NULL;
    if (self->gnu_hash.buckets_cnt > 0)
      sym = xdl_dynsym_find_symbol_use_gnu_hash(self, name);
    if (NULL == sym && self->sysv_hash.buckets_cnt > 0)
      sym = xdl_dynsym_find_symbol_use_sysv_hash(self, name);
    if (NULL != sym && !sh_metadata_accept(self, sym, self->dynstr, self->dynstr_sz,
                                           name, &address, &extent, &found)) return -1;
  }
  xdl_ensure_symtab_loaded(self);
  if (NULL != self->symtab) {
    for (size_t i = 0; i < self->symtab_cnt; ++i)
      if (!sh_metadata_accept(self, self->symtab + i, self->strtab, self->strtab_sz,
                              name, &address, &extent, &found)) return -1;
  }
  if (!found || address != (uintptr_t)expected) return -1;
  *begin = address;
  *size = extent;
  return 0;
}
