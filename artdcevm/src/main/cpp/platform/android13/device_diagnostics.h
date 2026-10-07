#pragma once
#include "platform/android13/device_profile.h"
#include "api/hook_engine.h"
#include <jni.h>
#include <cstdint>
namespace artdcevm::android13 {
bool ConfigureDeviceDiagnostics(HookEngine&,const DeviceProfile& profile,std::string*);
int DiagnosticCheckJniMode(JNIEnv*,int requested);
uint64_t DiagnosticReadBarrierCalls();
int DiagnosticJitMethodState(JNIEnv*,jobject);
bool DiagnosticAllMethodsDeoptimized();
int DiagnosticAotMethodState(JNIEnv*,jobject);
int DiagnosticJitInlineState(JNIEnv*,jobject);
int DiagnosticJitCodeKind(JNIEnv*,jobject);
int DiagnosticJitCacheDetail(JNIEnv*,jobject);
int DiagnosticInvalidateAllJit(JNIEnv*);
}
