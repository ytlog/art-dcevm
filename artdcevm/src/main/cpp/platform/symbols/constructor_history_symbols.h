#pragma once
#include "platform/symbols/art_symbol_compat.h"

namespace artdcevm::symbols::constructor_history {
// native/java_lang_Class.cc, fixed Android 11--17 FastNative declarations.
inline constexpr NamedSymbol kDeclaredConstructor{Library::kArt, "_ZN3artL36Class_getDeclaredConstructorInternalEP7_JNIEnvP8_jobjectP13_jobjectArray", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDeclaredConstructorUnique{Library::kArt, "_ZN3artL36Class_getDeclaredConstructorInternalEP7_JNIEnvP8_jobjectP13_jobjectArray.__uniq.181829539739573773940154555818377431209", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDeclaredConstructors{Library::kArt, "_ZN3artL37Class_getDeclaredConstructorsInternalEP7_JNIEnvP8_jobjecth", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDeclaredConstructorsUnique{Library::kArt, "_ZN3artL37Class_getDeclaredConstructorsInternalEP7_JNIEnvP8_jobjecth.__uniq.181829539739573773940154555818377431209", SymbolTable::kAnyNamed};
// Standard JNI weak identity comparison; both operands belong to this cache.
// No caller JNI entry or its CheckJNI validation is replaced.
inline constexpr NamedSymbol kSameObject{Library::kArt, "_ZN3art3JNIILb0EE12IsSameObjectEP7_JNIEnvP8_jobjectS5_", SymbolTable::kAnyNamed};
// reflection.cc: ObjPtr arguments/results use their fixed single-pointer ABI.
inline constexpr NamedSymbol kCallingClass{Library::kArt, "_ZN3art15GetCallingClassEPNS_6ThreadEm", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kVerifyAccess{Library::kArt, "_ZN3art12VerifyAccessENS_6ObjPtrINS_6mirror6ObjectEEENS0_INS1_5ClassEEEjS5_", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::constructor_history
