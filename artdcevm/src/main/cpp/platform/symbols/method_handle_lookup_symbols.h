#pragma once
#include "platform/symbols/art_symbol_compat.h"

namespace artdcevm::symbols::method_handle_lookup {
// native/java_lang_Class.cc, fixed Android 11--17: FastNative JNI entry.
inline constexpr NamedSymbol kDeclaredMethod{Library::kArt, "_ZN3artL31Class_getDeclaredMethodInternalEP7_JNIEnvP8_jobjectP8_jstringP13_jobjectArray", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDeclaredMethodUnique{Library::kArt, "_ZN3artL31Class_getDeclaredMethodInternalEP7_JNIEnvP8_jobjectP8_jstringP13_jobjectArray.__uniq.181829539739573773940154555818377431209", SymbolTable::kAnyNamed};
// mirror/class.cc and jni/java_vm_ext.cc, fixed Android 11--17.
inline constexpr NamedSymbol kFindClassMethod{Library::kArt, "_ZN3art6mirror5Class15FindClassMethodENSt3__117basic_string_viewIcNS2_11char_traitsIcEEEES6_NS_11PointerSizeE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kFindInterfaceMethod{Library::kArt, "_ZN3art6mirror5Class19FindInterfaceMethodENSt3__117basic_string_viewIcNS2_11char_traitsIcEEEES6_NS_11PointerSizeE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kNewLocalRef{Library::kArt, "_ZN3art9JNIEnvExt11NewLocalRefEPNS_6mirror6ObjectE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDecodeGlobal{Library::kArt, "_ZN3art9JavaVMExt12DecodeGlobalEPv", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::method_handle_lookup
