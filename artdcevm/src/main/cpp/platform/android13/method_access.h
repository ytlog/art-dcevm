#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <jni.h>
namespace artdcevm::android13 {
enum class MethodAccessResult { Unavailable, Allowed, Exception };
bool ConfigureMethodAccess(HookEngine&,const DeviceProfile&,JNIEnv*,jobject,std::string*);
// Runnable caller holds the mutator lock, roots cache_handle, and keeps native
// method/referrer alive. Null referrer skips access checks. Does not clear errors.
MethodAccessResult CheckResolvedMethodAccess(void* linker,void* cache_handle,uint32_t index,
    void* method,void* referrer,uint32_t invoke_type,std::string* error);
}
