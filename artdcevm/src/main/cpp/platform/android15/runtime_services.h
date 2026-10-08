#pragma once
#include "api/hook_engine.h"
#include "platform/common/runtime_services.h"
#include <jni.h>
namespace artdcevm::android15 {
using RuntimeServices=runtime_services::Snapshot;
// Fixed-source candidate validated against public JNI and JVMTI observations.
// JVMTI agent must already be attached. Borrowed services remain owned by ART;
// this grants no class/Thread layout token.
bool CalibrateRuntimeServices(HookEngine&,JNIEnv*,RuntimeServices*,std::string* error);
}
