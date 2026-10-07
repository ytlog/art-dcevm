#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include "platform/android13/dex_cache_method_abi.h"
#include "evolution/class_schema.h"
#include "platform/android13/handle_root_scope.h"
#include <cstdint>
#include <jni.h>
namespace artdcevm::android13 {

bool ConfigureDexCacheMethodReader(HookEngine&,const DeviceProfile&,JNIEnv*,jobject,std::string*);
// Caller roots the cache and holds the shared mutator lock. A miss is success
// with a null output; false means the reader or declaration is unavailable.
bool ReadCachedMethod(void* cache,uint32_t index,void** output);
// Same root/lock requirements. Reads the explicit cache's immutable DEX metadata,
// never the referrer's cache. The result owns strings; allocation may throw.
bool ReadCachedMethodReference(void* cache,uint32_t index,evolution::MappedDexMethodReference*,std::string*);
enum class MethodClassResolution { Unavailable, Resolved, Exception };
// Cache/loader are rooted Handle slots. Result is rooted before returning;
// Exception preserves the exception installed by ART. Caller holds mutator lock.
MethodClassResolution ResolveCachedMethodClass(void* linker,void* cache_handle,void* loader_handle,
    uint32_t method_index,bool cached_method,HandleRootScope& output,size_t slot,std::string* error);
}
