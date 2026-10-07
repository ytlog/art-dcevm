#pragma once

#include <string_view>
#include "api/hook_engine.h"

namespace artdcevm::symbols {

// A profile is tied to actual ELF builds, not just an Android API level.
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

struct ArtSymbolProfile {
  int sdk;
  const char* vendor;
  const char* build;
  const char* art_build_id;
  const char* jvmti_build_id;
  const char* compiler_build_id;
};

const ArtSymbolProfile* FindProfile(int sdk, std::string_view art_build_id,
                                    std::string_view jvmti_build_id);
void* FindSymbol(HookEngine& hooks, NamedSymbol symbol);

}  // namespace artdcevm::symbols
