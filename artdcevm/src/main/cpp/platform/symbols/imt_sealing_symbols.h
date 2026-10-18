#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Runtime services that give a class its own interface method table. Hidden
// in newer runtimes, so they resolve from any named table.
namespace artdcevm::symbols::imt_sealing {
// Runtime::CreateImtConflictMethod(LinearAlloc*): a runtime method whose entry
// is the IMT conflict trampoline, with its own empty conflict table.
inline constexpr NamedSymbol kCreateImtConflictMethod{
    Library::kArt, "_ZN3art7Runtime23CreateImtConflictMethodEPNS_11LinearAllocE", SymbolTable::kAnyNamed};
// LinearAlloc::Alloc. Runtimes from ART 14 take a LinearAllocKind; a runtime
// has exactly one of the two.
inline constexpr NamedSymbol kLinearAllocAlloc{
    Library::kArt, "_ZN3art11LinearAlloc5AllocEPNS_6ThreadEm", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kLinearAllocAllocKind{
    Library::kArt, "_ZN3art11LinearAlloc5AllocEPNS_6ThreadEmNS_15LinearAllocKindE", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::imt_sealing
