#pragma once
#include "platform/symbols/art_symbol_compat.h"

namespace artdcevm::symbols::runtime_thread {
inline constexpr NamedSymbol kCurrentThread{Library::kArt, "_ZN3art6Thread14CurrentFromGdbEv"};
inline constexpr NamedSymbol kReadBarrierMark{Library::kArt, "_ZN3art11ReadBarrier4MarkEPNS_6mirror6ObjectE", SymbolTable::kAnyNamed};
// Runtime-selected CC versus userfaultfd CMC, when exported (ART 14+).
inline constexpr NamedSymbol kUseReadBarrier{Library::kArt, "_ZN3art15gUseReadBarrierE", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::runtime_thread
