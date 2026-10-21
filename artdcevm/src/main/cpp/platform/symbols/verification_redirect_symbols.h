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
inline constexpr NamedSymbol kLegacyComponent{Library::kArt, "_ZN3art8verifier12RegTypeCache16GetComponentTypeERKNS0_7RegTypeENS_6ObjPtrINS_6mirror11ClassLoaderEEE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kModernComponent{Library::kArt, "_ZN3art8verifier12RegTypeCache16GetComponentTypeERKNS0_7RegTypeE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kLegacyAssignable{Library::kArt, "_ZN3art8verifier7RegType14AssignableFromERKS1_S3_bPNS0_14MethodVerifierE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kModernAssignable{Library::kArt, "_ZNK3art8verifier14MethodVerifier14AssignableFromERKNS0_7RegTypeES4_b", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kLegacyFrom{Library::kArt, "_ZN3art8verifier12RegTypeCache4FromENS_6ObjPtrINS_6mirror11ClassLoaderEEEPKcb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kLegacyFromClass{Library::kArt, "_ZN3art8verifier12RegTypeCache9FromClassEPKcNS_6ObjPtrINS_6mirror5ClassEEEb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kModernFrom{Library::kArt, "_ZN3art8verifier12RegTypeCache4FromEPKc", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kLegacyConstruct{Library::kArt, "_ZN3art8verifier12RegTypeCacheC1EPNS_11ClassLinkerEbRNS_20ScopedArenaAllocatorEb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kModernConstruct{Library::kArt, "_ZN3art8verifier12RegTypeCacheC1EPNS_6ThreadEPNS_11ClassLinkerEPNS_9ArenaPoolENS_6HandleINS_6mirror11ClassLoaderEEEPKNS_7DexFileEbb", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::verification_redirect
