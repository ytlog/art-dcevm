#pragma once
#include "api/hook_engine.h"
#include "platform/common/runtime_services.h"
#include <jni.h>
namespace artdcevm::runtime_services {
// Named original receivers and public VM attachment prove service ownership.
// This does not select or authorize a Runtime layout from SDK or BuildID.
bool CalibrateRuntimeServices(HookEngine&,JNIEnv*,Snapshot*,std::string* error);
}
