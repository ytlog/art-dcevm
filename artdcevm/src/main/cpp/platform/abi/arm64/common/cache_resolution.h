#pragma once
#include "platform/abi/arm64/android11/jni_object_decode.h"
namespace artdcevm::abi::arm64::android_common {
// Pinned class_linker.h (11/15/17): one compressed Handle slot pointer per
// by-value Handle<T>; dex::TypeIndex wraps uint16_t. Calls may suspend.
struct CacheTypeIndex {uint16_t index;};
using ResolveCacheType=android11::ReleaseObjectPointer(*)(void*,CacheTypeIndex,const void*,const void*);
// obj_ptr.h: one uintptr_t reference_, trivially copyable in the already
// independently observed release/no-poison representation (also used by JNI
// DecodeJObject/Class lookup). Three by-value ObjPtr arguments, NOT Handles.
using FindCacheField=void*(*)(void*,android11::ReleaseObjectPointer,
 android11::ReleaseObjectPointer,android11::ReleaseObjectPointer,uint32_t,bool);
inline constexpr char kFindCacheField[]="_ZN3art11ClassLinker17FindResolvedFieldENS_6ObjPtrINS_6mirror5ClassEEENS1_INS2_8DexCacheEEENS1_INS2_11ClassLoaderEEEjb";
using ResolveCacheMember=void*(*)(void*,uint32_t,const void*,const void*);
inline constexpr char kResolveCacheType[]="_ZN3art11ClassLinker13DoResolveTypeENS_3dex9TypeIndexENS_6HandleINS_6mirror8DexCacheEEENS3_INS4_11ClassLoaderEEE";
inline constexpr char kResolveCacheField[]="_ZN3art11ClassLinker15ResolveFieldJLSEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE";
inline constexpr char kResolveCacheMethodId[]="_ZN3art11ClassLinker15ResolveMethodIdEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE";
inline constexpr char kResolveCacheMethodLegacy[]="_ZN3art11ClassLinker30ResolveMethodWithoutInvokeTypeEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE";
static_assert(sizeof(CacheTypeIndex)==2);
}
