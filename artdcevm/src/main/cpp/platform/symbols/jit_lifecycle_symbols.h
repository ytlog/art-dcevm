#pragma once
#include "platform/symbols/art_symbol_compat.h"
namespace artdcevm::symbols::jit_lifecycle {
inline constexpr NamedSymbol kRemoveMethodLocked{Library::kArt,
    "_ZN3art3jit12JitCodeCache18RemoveMethodLockedEPNS_9ArtMethodEb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kFreeCodeAndData{Library::kArt,
    "_ZN3art3jit12JitCodeCache15FreeCodeAndDataEPKv", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::jit_lifecycle
