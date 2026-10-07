#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <jni.h>
namespace artdcevm::android13 {
enum class MethodInvokeResult { Unavailable, Compatible, Mismatch, Exception };
bool ConfigureMethodInvoke(HookEngine&,const DeviceProfile&,JNIEnv*,jobject,std::string*);
// Runnable caller holds the mutator lock and keeps native methods alive.
// The check does not install an exception. Valid ART InvokeType values are 0..5.
MethodInvokeResult CheckMethodInvokeType(void* method,uint32_t invoke_type);
// Checks the referenced class before method access/find checks. Caller roots
// both Handle slots; a class mismatch immediately installs the named ART ICCE.
MethodInvokeResult CheckClassInvokeType(void* class_handle,void* cache_handle,uint32_t method_index,
    uint32_t invoke_type,std::string* error);
// Installs the named ART method ICCE; null referrer is allowed. Does not clear it.
MethodInvokeResult ThrowMethodInvokeMismatch(void* method,void* referrer,uint32_t invoke_type);
}
