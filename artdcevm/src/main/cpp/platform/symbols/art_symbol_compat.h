#pragma once
#include "shadowhook.h"

namespace artdcevm::symbols {

enum class Library { kArt, kJvmti };

constexpr const char* LibraryName(Library library) {
  return library == Library::kArt ? "libart.so" : "libopenjdkjvmti.so";
}

// A named ELF symbol. Every catalogued symbol is a .dynsym entry; addresses
// and offsets are never derived from disassembly.
struct NamedSymbol {
  Library library;
  const char* name;
};

// Official ShadowHook lookup restricted to .dynsym.
inline void* Find(NamedSymbol symbol) {
  void* handle = shadowhook_dlopen(LibraryName(symbol.library));
  if (!handle) return nullptr;
  void* address = shadowhook_dlsym_dynsym(handle, symbol.name);
  shadowhook_dlclose(handle);
  return address;
}

}  // namespace artdcevm::symbols
