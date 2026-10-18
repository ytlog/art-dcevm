#pragma once
#include "shadowhook.h"

namespace artdcevm::symbols {

enum class Library { kArt, kJvmti, kDexFile };
enum class SymbolTable { kDynsym, kAnyNamed };

constexpr const char* LibraryName(Library library) {
  return library == Library::kArt ? "libart.so" : library == Library::kJvmti ? "libopenjdkjvmti.so" : "libdexfile.so";
}

// A named ELF symbol. Addresses and offsets are never derived from disassembly.
struct NamedSymbol {
  Library library;
  const char* name;
  SymbolTable table = SymbolTable::kDynsym;
};

// Official ShadowHook lookup. Only explicitly catalogued local symbols may
// use .symtab or its compressed .gnu_debugdata copy.
inline void* Find(NamedSymbol symbol) {
  void* handle = shadowhook_dlopen(LibraryName(symbol.library));
  if (!handle) return nullptr;
  void* address = symbol.table == SymbolTable::kDynsym
      ? shadowhook_dlsym_dynsym(handle, symbol.name) : shadowhook_dlsym(handle, symbol.name);
  shadowhook_dlclose(handle);
  return address;
}

}  // namespace artdcevm::symbols
