#pragma once
#include "platform/abi/arm64/android11/jni_object_decode.h"
namespace artdcevm::abi::arm64::android_common {
// Pinned class_linker.h (11/15/17): one compressed Handle slot pointer per
// by-value Handle<T>; dex::TypeIndex wraps uint16_t. Calls may suspend.
struct CacheTypeIndex {uint16_t index;};
using ResolveCacheType=android11::ReleaseObjectPointer(*)(void*,CacheTypeIndex,const void*,const void*);
using ResolveCacheMember=void*(*)(void*,uint32_t,const void*,const void*);
inline constexpr char kResolveCacheType[]="_ZN3art11ClassLinker13DoResolveTypeENS_3dex9TypeIndexENS_6HandleINS_6mirror8DexCacheEEENS3_INS4_11ClassLoaderEEE";
inline constexpr char kResolveCacheField[]="_ZN3art11ClassLinker15ResolveFieldJLSEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE";
inline constexpr char kResolveCacheMethodId[]="_ZN3art11ClassLinker15ResolveMethodIdEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE";
inline constexpr char kResolveCacheMethodLegacy[]="_ZN3art11ClassLinker30ResolveMethodWithoutInvokeTypeEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE";
static_assert(sizeof(CacheTypeIndex)==2);
}
