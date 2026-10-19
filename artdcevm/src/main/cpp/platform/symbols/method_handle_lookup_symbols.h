#pragma once
#include "platform/symbols/art_symbol_compat.h"

namespace artdcevm::symbols::method_handle_lookup {
// native/java_lang_Class.cc, fixed Android 11--17: FastNative JNI entry.
inline constexpr NamedSymbol kDeclaredMethod{Library::kArt, "_ZN3artL31Class_getDeclaredMethodInternalEP7_JNIEnvP8_jobjectP8_jstringP13_jobjectArray", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDeclaredMethodUnique{Library::kArt, "_ZN3artL31Class_getDeclaredMethodInternalEP7_JNIEnvP8_jobjectP8_jstringP13_jobjectArray.__uniq.181829539739573773940154555818377431209", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::method_handle_lookup
