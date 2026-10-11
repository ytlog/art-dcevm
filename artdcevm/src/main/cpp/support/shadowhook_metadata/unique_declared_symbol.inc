/* ART-DCEVM strict declaration-name lookup on the existing official xDL cache.
 * No file parsing, decompression, address arithmetic, or second loader here.
 * Exact data/function declarations retain official lookup semantics. Only a
 * missing declaration can select one uniquely named numeric function clone.
 */
static bool sh_declared_numeric_clone(const char *full, const char *name) {
  size_t n = strlen(name);
  if (strchr(name, '.') != NULL || strchr(name, '$') != NULL ||
      strlen(full) <= n + 8 || memcmp(full, name, n) != 0 ||
      memcmp(full + n, ".__uniq.", 8) != 0) return false;
  const char *suffix = full + n + 8;
  if (*suffix < '0' || *suffix > '9') return false;
  do { ++suffix; } while (*suffix >= '0' && *suffix <= '9');
  if (*suffix == 0) return true;
  if (strncmp(suffix, ".llvm.", 6) != 0) return false;
  suffix += 6;
  if (*suffix < '0' || *suffix > '9') return false;
  do { ++suffix; } while (*suffix >= '0' && *suffix <= '9');
  return *suffix == 0;
}

void *shadowhook_dlsym_declared_symbol_symtab(void *handle, const char *declaration);
void *shadowhook_dlsym_declared_symbol_symtab(void *handle, const char *declaration) {
  if (NULL == handle || NULL == declaration || declaration[0] == 0) return NULL;
  xdl_t *self = handle;
  xdl_ensure_symtab_loaded(self);
  if (NULL == self->symtab || NULL == self->strtab) return NULL;
  ElfW(Sym) *exact = NULL, *clone = NULL;
  bool exact_ambiguous = false, clone_ambiguous = false;
  for (size_t i = 0; i < self->symtab_cnt; ++i) {
    ElfW(Sym) *sym = self->symtab + i;
    if (!XDL_SYMTAB_IS_EXPORT_SYM(sym->st_shndx)) continue;
    if (sym->st_name >= self->strtab_sz) return NULL;
    const char *name = self->strtab + sym->st_name;
    if (NULL == memchr(name, 0, self->strtab_sz - sym->st_name)) return NULL;
    if (strcmp(name, declaration) == 0) {
      if (exact != NULL) exact_ambiguous = true;
      exact = sym;
    } else if (ELF64_ST_TYPE(sym->st_info) == STT_FUNC && sh_declared_numeric_clone(name, declaration)) {
      if (clone != NULL) clone_ambiguous = true;
      clone = sym;
    }
  }
  if (exact_ambiguous || (exact == NULL && clone_ambiguous)) return NULL;
  ElfW(Sym) *selected = exact != NULL ? exact : clone;
  if (NULL == selected) return NULL;
  // The declaration is selected strictly from the official loaded symbol cache.
  // Use the unchanged official resolver directly: its broader canonical lookup
  // must not substitute an earlier clone or another '.'/'$' prefix symbol.
  // Address calculation and IFUNC handling remain exclusively inside xDL.
  return xdl_resolve_symbol_address(self, selected, NULL);
}
