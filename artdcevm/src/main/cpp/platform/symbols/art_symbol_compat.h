#pragma once

#include <string>
#include "api/hook_engine.h"

namespace artdcevm::symbols {

enum class Library { kArt, kJvmti, kCompiler, kLinker };
enum class HookRoute { kInline, kPlt };

constexpr const char* LibraryName(Library library) {
  switch (library) {
    case Library::kArt: return "libart.so";
    case Library::kJvmti: return "libopenjdkjvmti.so";
    case Library::kCompiler: return "libart-compiler.so";
    case Library::kLinker: return "linker64";
  }
  return nullptr;
}

struct NamedSymbol {
  Library library;
  const char* name;
  // PLT is valid only when every required caller imports this symbol through a
  // relocation. Same-DSO direct calls require an inline hook.
  HookRoute route;
};

// Presence is only the first gate. Source ABI layout is checked by
// ArtDcevmSourceProbe before any source callbacks are installed.
bool CheckValidationSymbols(HookEngine& hooks, std::string* error);
bool CheckSourceSymbols(HookEngine& hooks, std::string* error);
void* FindSymbol(HookEngine& hooks, NamedSymbol symbol);

}  // namespace artdcevm::symbols
