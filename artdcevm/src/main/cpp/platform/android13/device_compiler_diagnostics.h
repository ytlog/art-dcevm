#pragma once
#include "platform/android13/device_profile.h"
#include "api/hook_engine.h"
#include <jni.h>
#include <cstdint>
#include <string>
namespace artdcevm::android13 {
bool ConfigureDeviceCompilerDiagnostics(HookEngine&,const DeviceProfile& profile,std::string*);
int DiagnosticForceOptimizedJit(JNIEnv*,jobject);
int DiagnosticJitInlineExperiment(JNIEnv*,bool);
int DiagnosticAotExperiment(JNIEnv*,jobject,bool);
int DiagnosticJitCompilerOptionsState();
int DiagnosticPauseJitForGraphHook(bool);
int DiagnosticConfigureGraphInlineProbe(JNIEnv*,jobject,jobject,void* original);
void* DiagnosticHGraphInlineInto(void*,void*,void*);
uint64_t DiagnosticGraphInlineHits();
}
