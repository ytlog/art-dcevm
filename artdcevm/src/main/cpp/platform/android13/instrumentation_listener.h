#pragma once
#include "api/hook_engine.h"
#include <jni.h>
#include <cstdint>
#include <string>
namespace artdcevm::android13 {
bool ConfigureInstrumentationListener(HookEngine&,void* source,JNIEnv*,jobject,std::string*);
bool SetReceiverGuardMode(bool enable,bool force_reject=false);
uint64_t FieldReadProbeHits();
uint64_t InvokeGuardDiagnostics(int index);
void SetDeoptTracing(bool enabled);
void TraceDeopt(void* thread,void* frames,void* result,bool from_code,int type);
}
