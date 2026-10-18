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
// entrypoint_utils-inl.h, Android 14--17: switch interpreter invocation selection.
// Absent where a runtime inlines it (all of 11--13, some 14 and 17 modules).
inline constexpr NamedSymbol kSelectVirtual{Library::kArt, "_ZN3art16FindMethodToCallILNS_10InvokeTypeE2EEEPNS_9ArtMethodEPNS_6ThreadES3_PNS_6ObjPtrINS_6mirror6ObjectEEERKNS_11InstructionEbPb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kSelectInterface{Library::kArt, "_ZN3art16FindMethodToCallILNS_10InvokeTypeE4EEEPNS_9ArtMethodEPNS_6ThreadES3_PNS_6ObjPtrINS_6mirror6ObjectEEERKNS_11InstructionEbPb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kMethodDescriptor{Library::kArt, "_ZN3art9ArtMethod27GetDeclaringClassDescriptorEv", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kMethodLoader{Library::kArt, "_ZN3art9ArtMethod14GetClassLoaderEv", SymbolTable::kAnyNamed};
// art_method.cc: copied obsolete defaults live outside the declaring interface's array.
// Android 11--13 use the non-template read-barrier entry.
inline constexpr NamedSymbol kObsoleteCacheLegacy{Library::kArt, "_ZN3art9ArtMethod19GetObsoleteDexCacheEv", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kObsoleteCacheRead{Library::kArt, "_ZN3art9ArtMethod19GetObsoleteDexCacheILNS_17ReadBarrierOptionE0EEENS_6ObjPtrINS_6mirror8DexCacheEEEv", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kObsoleteCacheRaw{Library::kArt, "_ZN3art9ArtMethod19GetObsoleteDexCacheILNS_17ReadBarrierOptionE1EEENS_6ObjPtrINS_6mirror8DexCacheEEEv", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::invoke_kind
