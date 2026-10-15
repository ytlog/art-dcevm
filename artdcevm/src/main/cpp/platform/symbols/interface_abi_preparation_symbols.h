#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Offline inventory only; no unverified release is activated by this catalog.
namespace artdcevm::symbols::interface_abi_preparation {
// android-11.0.0_r1 thread.h: pid_t thread_id, rather than uint32_t since 12.
inline constexpr NamedSymbol kHandleScopeVisitRootsSigned{Library::kArt,
    "_ZN3art6Thread21HandleScopeVisitRootsEPNS_11RootVisitorEi", SymbolTable::kAnyNamed};
// Older array allocation declarations also require ART's current AllocatorType.
inline constexpr NamedSymbol kObjectArrayAllocWithAllocator{Library::kArt,
    "_ZN3art6mirror11ObjectArrayINS0_6ObjectEE5AllocEPNS_6ThreadENS_6ObjPtrINS0_5ClassEEEiNS_2gc13AllocatorTypeE",
    SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kClassArrayAllocWithAllocator{Library::kArt,
    "_ZN3art6mirror11ObjectArrayINS0_5ClassEE5AllocEPNS_6ThreadENS_6ObjPtrIS2_EEiNS_2gc13AllocatorTypeE",
    SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::interface_abi_preparation
