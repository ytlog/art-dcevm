#pragma once
#include "platform/symbols/art_symbol_compat.h"

namespace artdcevm::symbols::jni_interface_dispatch {
// reflection.cc specializations are reached after CheckJNI's receiver checks.
inline constexpr NamedSymbol kVirtualVarArgs{Library::kArt, "_ZN3art35InvokeVirtualOrInterfaceWithVarArgsIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_St9__va_list", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kVirtualJValues{Library::kArt, "_ZN3art35InvokeVirtualOrInterfaceWithJValuesIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDirectVarArgs{Library::kArt, "_ZN3art17InvokeWithVarArgsIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_St9__va_list", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDirectJValues{Library::kArt, "_ZN3art17InvokeWithJValuesIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::jni_interface_dispatch
