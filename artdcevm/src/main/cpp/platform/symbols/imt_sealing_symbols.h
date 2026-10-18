#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Runtime services that give a class its own interface method table. Hidden
// in newer runtimes, so they resolve from any named table.
namespace artdcevm::symbols::imt_sealing {
// runtime.h/runtime-inl.h: the process-wide conflict method. ART creates a
// private slot cache when AddMethodToConflictTable sees this canonical method.
inline constexpr NamedSymbol kGetImtConflictMethod{Library::kArt, "_ZN3art7Runtime20GetImtConflictMethodEv", SymbolTable::kAnyNamed};
// class_linker.cc, ART 11--17: before LinkClass installs the IMT and publishes
// kResolved. ART 13+ may inline this wrapper; its out-of-line k64 helper has
// the same explicit arguments and is preferred when present.
inline constexpr NamedSymbol kLinkMethods{Library::kArt, "_ZN3art11ClassLinker11LinkMethodsEPNS_6ThreadENS_6HandleINS_6mirror5ClassEEENS3_INS4_11ObjectArrayIS5_EEEEPbPPNS_9ArtMethodE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kLinkMethods64{Library::kArt, "_ZN3art11ClassLinker17LinkMethodsHelperILNS_11PointerSizeE8EE11LinkMethodsEPNS_6ThreadENS_6HandleINS_6mirror5ClassEEENS6_INS7_11ObjectArrayIS8_EEEEPbPPNS_9ArtMethodE", SymbolTable::kAnyNamed};
// class_linker.h, Android 11/12: this whole-table pre-publication path
// receives the canonical conflict method as an explicit argument.
inline constexpr NamedSymbol kFillImtFromSuperClass{Library::kArt, "_ZN3art11ClassLinker21FillImtFromSuperClassENS_6HandleINS_6mirror5ClassEEEPNS_9ArtMethodES6_PbPS6_", SymbolTable::kAnyNamed};
// LinearAlloc::Alloc. Runtimes from ART 14 take a LinearAllocKind; a runtime
// has exactly one of the two.
inline constexpr NamedSymbol kLinearAllocAlloc{
    Library::kArt, "_ZN3art11LinearAlloc5AllocEPNS_6ThreadEm", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kLinearAllocAllocKind{
    Library::kArt, "_ZN3art11LinearAlloc5AllocEPNS_6ThreadEmNS_15LinearAllocKindE", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::imt_sealing
