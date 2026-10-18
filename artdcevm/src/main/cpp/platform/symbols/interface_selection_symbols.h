#pragma once
#include "platform/symbols/art_symbol_compat.h"

namespace artdcevm::symbols::interface_selection {
inline constexpr NamedSymbol kOpenDex{Library::kDexFile, "_ZN3art13DexFileLoader10OpenCommonEPKhmS2_mRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPKNS_10OatDexFileEbbPS9_NS3_10unique_ptrINS_16DexFileContainerENS3_14default_deleteISH_EEEEPNS0_12VerifyResultE"};
inline constexpr NamedSymbol kAllocMethods{Library::kArt, "_ZN3art11ClassLinker19AllocArtMethodArrayEPNS_6ThreadEPNS_11LinearAllocEm", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kGetAllocator{Library::kArt, "_ZN3art11ClassLinker26GetAllocatorForClassLoaderENS_6ObjPtrINS_6mirror11ClassLoaderEEE"};
inline constexpr NamedSymbol kAllocPointers{Library::kArt, "_ZN3art11ClassLinker17AllocPointerArrayEPNS_6ThreadEm", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kUpdateMethods{Library::kArt, "_ZN3art11ClassLinker18UpdateClassMethodsENS_6ObjPtrINS_6mirror5ClassEEEPNS_19LengthPrefixedArrayINS_9ArtMethodEEE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kAddGlobal{Library::kArt, "_ZN3art9JavaVMExt12AddGlobalRefEPNS_6ThreadENS_6ObjPtrINS_6mirror6ObjectEEE"};
inline constexpr NamedSymbol kDeleteGlobal{Library::kArt, "_ZN3art9JavaVMExt15DeleteGlobalRefEPNS_6ThreadEP8_jobject"};
inline constexpr NamedSymbol kInvocationError{Library::kArt, "_ZN3art9ArtMethod24ThrowInvocationTimeErrorEv", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kInvocationReceiverError{Library::kArt, "_ZN3art9ArtMethod24ThrowInvocationTimeErrorENS_6ObjPtrINS_6mirror6ObjectEEE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kAbstractError{Library::kArt, "_ZN3art24ThrowAbstractMethodErrorEPNS_9ArtMethodE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kAbstractReceiverError{Library::kArt, "_ZN3art24ThrowAbstractMethodErrorEPNS_9ArtMethodENS_6ObjPtrINS_6mirror6ObjectEEE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kInterpreterCall{Library::kArt, "_ZN3art11interpreter33ArtInterpreterToInterpreterBridgeEPNS_6ThreadERKNS_20CodeItemDataAccessorEPNS_11ShadowFrameEPNS_6JValueE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kInterpreterBridge{Library::kArt, "art_quick_to_interpreter_bridge", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::interface_selection
