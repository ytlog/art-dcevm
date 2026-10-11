#pragma once
#include "hooks/shadow_hook.h"

namespace artdcevm::symbols {

enum class Library { kArt, kJvmti };
enum class SymbolTable { kDynamic, kAnyNamed };

constexpr const char* LibraryName(Library library) {
  return library == Library::kArt ? "libart.so" : "libopenjdkjvmti.so";
}

// A named ELF symbol. Addresses and offsets are never derived from disassembly.
struct NamedSymbol {
  Library library;
  const char* name;
  SymbolTable table = SymbolTable::kDynamic;
};

inline void* Find(NamedSymbol symbol) {
  return hooks::FindSymbol(LibraryName(symbol.library), symbol.name,
                           symbol.table == SymbolTable::kAnyNamed);
}

}  // namespace artdcevm::symbols
