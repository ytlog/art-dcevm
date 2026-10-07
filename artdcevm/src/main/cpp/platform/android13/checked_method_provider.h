#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <jni.h>
namespace artdcevm::android13 {
bool ConfigureCheckedMethodProvider(HookEngine&,const DeviceProfile&,JNIEnv*,jobject,std::string*);
using CheckedHandleResolver=void*(*)(void*,uint32_t,void*,void*,void*,uint32_t);
CheckedHandleResolver SelectCheckedHandleResolver(HookEngine&,bool force_source,std::string*);
// SDK implementation of the checked explicit-Handle resolver. The runnable
// caller owns cache/loader Handle slots. After calibration every null result
// installs an ART error; initialization must complete before this entry is used.
void* SourceResolveCheckedHandles(void* linker,uint32_t index,void* cache_handle,
                                 void* loader_handle,void* referrer,uint32_t invoke_type);
}
