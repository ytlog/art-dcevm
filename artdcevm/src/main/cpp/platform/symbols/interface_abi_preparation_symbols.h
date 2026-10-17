#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Offline inventory only; no unverified release is activated by this catalog.
namespace artdcevm::symbols::interface_abi_preparation {
// Historical allocation names only. The backend prepares JNI arrays while
// Native and no longer calls these wrappers or chooses ART's allocator.
inline constexpr NamedSymbol kObjectArrayAlloc{Library::kArt,
    "_ZN3art6mirror11ObjectArrayINS0_6ObjectEE5AllocEPNS_6ThreadENS_6ObjPtrINS0_5ClassEEEi",
    SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kClassArrayAlloc{Library::kArt,
    "_ZN3art6mirror11ObjectArrayINS0_5ClassEE5AllocEPNS_6ThreadENS_6ObjPtrIS2_EEi",
    SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kObjectArrayAllocWithAllocator{Library::kArt,
    "_ZN3art6mirror11ObjectArrayINS0_6ObjectEE5AllocEPNS_6ThreadENS_6ObjPtrINS0_5ClassEEEiNS_2gc13AllocatorTypeE",
    SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kClassArrayAllocWithAllocator{Library::kArt,
    "_ZN3art6mirror11ObjectArrayINS0_5ClassEE5AllocEPNS_6ThreadENS_6ObjPtrIS2_EEiNS_2gc13AllocatorTypeE",
    SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::interface_abi_preparation
