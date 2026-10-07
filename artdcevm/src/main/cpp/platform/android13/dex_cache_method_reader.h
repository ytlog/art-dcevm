#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include "platform/android13/dex_cache_method_abi.h"
#include <cstdint>
#include <jni.h>
namespace artdcevm::android13 {

bool ConfigureDexCacheMethodReader(HookEngine&,const DeviceProfile&,JNIEnv*,jobject,std::string*);
// Caller roots the cache and holds the shared mutator lock. A miss is success
// with a null output; false means the reader or declaration is unavailable.
bool ReadCachedMethod(void* cache,uint32_t index,void** output);
}
