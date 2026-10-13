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
// Constructor.newInstance0's native implementation (java_lang_reflect_Constructor.cc),
// spelled like Method_invoke above.
inline constexpr NamedSymbol kConstructorNewInstanceNative[] = {
    {Library::kArt, "_ZN3artL24Constructor_newInstance0EP7_JNIEnvP8_jobjectP13_jobjectArray", SymbolTable::kAnyNamed},
    {Library::kArt, "_ZN3artL24Constructor_newInstance0EP7_JNIEnvP8_jobjectP13_jobjectArray.__uniq.114100828369361150012403296395805223021", SymbolTable::kAnyNamed}};
// JniIdManager::EncodeFieldId overloads. Newer ART adds a Handle<Class>
// parameter; a runtime has one of the two pairs.
inline constexpr NamedSymbol kEncodeFieldId{Library::kArt, "_ZN3art3jni12JniIdManager13EncodeFieldIdEPNS_8ArtFieldE"};
inline constexpr NamedSymbol kEncodeFieldIdHandle{Library::kArt, "_ZN3art3jni12JniIdManager13EncodeFieldIdENS_16ReflectiveHandleINS_8ArtFieldEEE"};
inline constexpr NamedSymbol kEncodeFieldIdWithClass{Library::kArt, "_ZN3art3jni12JniIdManager13EncodeFieldIdENS_6HandleINS_6mirror5ClassEEEPNS_8ArtFieldE"};
inline constexpr NamedSymbol kEncodeFieldIdHandleWithClass{Library::kArt, "_ZN3art3jni12JniIdManager13EncodeFieldIdENS_6HandleINS_6mirror5ClassEEENS_16ReflectiveHandleINS_8ArtFieldEEE"};
// Optional: Android 13 added it for transformer handles; it forwards to the
// guarded MethodHandleInvokeExact. Android 11/12 transformers use
// invoke-polymorphic, so its absence leaves no unguarded path.
inline constexpr NamedSymbol kMethodHandleInvokeExactWithFrame{Library::kArt, "_ZN3art32MethodHandleInvokeExactWithFrameEPNS_6ThreadENS_6HandleINS_6mirror12MethodHandleEEENS2_INS3_18EmulatedStackFrameEEE", SymbolTable::kAnyNamed};
// ClassLinker::FindClass by descriptor. Android 16 added the length
// parameter; every collected runtime has at least one of the two.
inline constexpr NamedSymbol kFindClass{Library::kArt, "_ZN3art11ClassLinker9FindClassEPNS_6ThreadEPKcNS_6HandleINS_6mirror11ClassLoaderEEE"};
inline constexpr NamedSymbol kFindClassWithLength{Library::kArt, "_ZN3art11ClassLinker9FindClassEPNS_6ThreadEPKcmNS_6HandleINS_6mirror11ClassLoaderEEE"};
}  // namespace artdcevm::symbols::method_retirement
