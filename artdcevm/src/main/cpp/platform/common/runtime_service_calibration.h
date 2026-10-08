#pragma once
#include "api/hook_engine.h"
#include "platform/common/runtime_services.h"
#include <jni.h>
namespace artdcevm::runtime_services {
// Named listener ABI + independently observed service receivers select the
// fixed Runtime declaration. This never selects from SDK or BuildID.
bool CalibrateRuntimeServices(HookEngine&,JNIEnv*,Snapshot*,std::string* error);
}
