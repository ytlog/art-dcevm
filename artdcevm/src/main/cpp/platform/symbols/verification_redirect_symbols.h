#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Verifier-redirect entry points that are not one universal .dynsym name.
// They stay out of the strict catalog; the runtime resolves them by exact name.
namespace artdcevm::symbols::verification_redirect {
// ClassLinker::FindClass by descriptor. Android 16 added the length
// parameter; every collected runtime has at least one of the two.
inline constexpr NamedSymbol kFindClass{Library::kArt, "_ZN3art11ClassLinker9FindClassEPNS_6ThreadEPKcNS_6HandleINS_6mirror11ClassLoaderEEE"};
inline constexpr NamedSymbol kFindClassWithLength{Library::kArt, "_ZN3art11ClassLinker9FindClassEPNS_6ThreadEPKcmNS_6HandleINS_6mirror11ClassLoaderEEE"};
inline constexpr NamedSymbol kRegisterFindClass{Library::kArt, "_ZNK3art8verifier12RegTypeCache9FindClassENS_6ObjPtrINS_6mirror5ClassEEEb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kRegisterInsertClass{Library::kArt, "_ZN3art8verifier12RegTypeCache11InsertClassERKNSt3__117basic_string_viewIcNS2_11char_traitsIcEEEENS_6ObjPtrINS_6mirror5ClassEEEb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kRegisterFromClass{Library::kArt, "_ZN3art8verifier12RegTypeCache9FromClassENS_6ObjPtrINS_6mirror5ClassEEE", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::verification_redirect
