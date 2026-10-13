#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Method-retirement entry points that are not one universal .dynsym name.
// They stay out of the strict catalog; the runtime resolves them by exact name.
namespace artdcevm::symbols::method_retirement {
// Method.invoke's native implementation (java_lang_reflect_Method.cc). Builds
// spell this local function plainly or with one fixed .__uniq suffix; one of
// the two exists in every collected runtime.
inline constexpr NamedSymbol kMethodInvokeNative[] = {
    {Library::kArt, "_ZN3artL13Method_invokeEP7_JNIEnvP8_jobjectS3_P13_jobjectArray", SymbolTable::kAnyNamed},
    {Library::kArt, "_ZN3artL13Method_invokeEP7_JNIEnvP8_jobjectS3_P13_jobjectArray.__uniq.165753521025965369065708152063621506277", SymbolTable::kAnyNamed}};
// Optional: Android 13 added it for transformer handles; it forwards to the
// guarded MethodHandleInvokeExact. Android 11/12 transformers use
// invoke-polymorphic, so its absence leaves no unguarded path.
inline constexpr NamedSymbol kMethodHandleInvokeExactWithFrame{Library::kArt, "_ZN3art32MethodHandleInvokeExactWithFrameEPNS_6ThreadENS_6HandleINS_6mirror12MethodHandleEEENS2_INS3_18EmulatedStackFrameEEE", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::method_retirement
