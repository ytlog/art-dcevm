#pragma once
#include "platform/symbols/art_symbol_compat.h"

namespace artdcevm::symbols::invoke_kind {
// class_linker{,-inl}.h, Android 11--15: ResolveMode 0 skips invocation checks.
// The two overloads cover cached and cold lookups in the older interpreter.
inline constexpr NamedSymbol kResolveCaller{Library::kArt, "_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE0EEEPNS_9ArtMethodEPNS_6ThreadEjS4_NS_10InvokeTypeE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kResolveCache{Library::kArt, "_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE0EEEPNS_9ArtMethodEjNS_6HandleINS_6mirror8DexCacheEEENS5_INS6_11ClassLoaderEEES4_NS_10InvokeTypeE", SymbolTable::kAnyNamed};
// Android 16--17: unchecked ID resolution no longer receives an invoke type.
inline constexpr NamedSymbol kResolveMethodId{Library::kArt, "_ZN3art11ClassLinker15ResolveMethodIdEjPNS_9ArtMethodE", SymbolTable::kAnyNamed};
// nterp.cc. Android 14+ also uses this cache-fill entry in the switch interpreter.
inline constexpr NamedSymbol kNterpGetMethod{Library::kArt, "NterpGetMethod", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::invoke_kind
