#pragma once
#include "hooks/shadow_hook.h"

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

inline void* Find(NamedSymbol symbol) { return hooks::FindSymbol(LibraryName(symbol.library), symbol.name); }

}  // namespace artdcevm::symbols
