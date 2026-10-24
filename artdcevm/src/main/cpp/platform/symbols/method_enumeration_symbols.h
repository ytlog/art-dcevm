#pragma once
#include "platform/symbols/art_symbol_compat.h"

namespace artdcevm::symbols::method_enumeration {
// native/java_lang_Class.cc, fixed Android 11--17: FastNative JNI entry.
inline constexpr NamedSymbol kDeclaredMethods{Library::kArt, "_ZN3artL33Class_getDeclaredMethodsUncheckedEP7_JNIEnvP8_jobjecth", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDeclaredMethodsUnique{Library::kArt, "_ZN3artL33Class_getDeclaredMethodsUncheckedEP7_JNIEnvP8_jobjecth.__uniq.181829539739573773940154555818377431209", SymbolTable::kAnyNamed};
}
