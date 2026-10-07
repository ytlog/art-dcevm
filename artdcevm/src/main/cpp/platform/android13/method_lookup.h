#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <jni.h>
namespace artdcevm::android13 {
bool ConfigureMethodLookup(HookEngine&,const DeviceProfile&,JNIEnv*,jobject,std::string*);
// Runnable caller roots Class/DexCache/ClassLoader Handle slots and holds the
// shared mutator lock. A null result is a legitimate miss, not an exception.
bool FindCheckedMethod(void* linker,void* class_handle,void* cache_handle,void* loader_handle,
                       uint32_t index,bool incompatible,void** output);
// Installs named ART NoSuchMethodError using this explicit cache's name/prototype.
// Class and cache Handle slots remain rooted; bad_alloc propagates to the boundary.
bool ThrowMissingMethod(void* class_handle,void* cache_handle,uint32_t index,uint32_t invoke_type,std::string*);
}
