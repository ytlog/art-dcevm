#pragma once
#include "platform/android13/device_profile.h"
#include "api/hook_engine.h"
#include <jni.h>
#include <cstdint>
namespace artdcevm::android13 {
bool ConfigureCompiledFrameDiagnostics(HookEngine&,const DeviceProfile& profile,std::string*);
int DiagnosticAotFrameState(JNIEnv*,jobject peer,jobject executable);
int DiagnosticJitActiveFrameState(JNIEnv*,jobject peer,jobject caller,jobject callee);
int DiagnosticAotSuspendDeoptProbe(JNIEnv*,jobject peer,jobject executable);
uint64_t DiagnosticAotSuspendDeoptHits(bool reset);
}
