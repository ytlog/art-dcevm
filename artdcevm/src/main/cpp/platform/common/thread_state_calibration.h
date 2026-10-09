#pragma once
#include "api/hook_engine.h"
#include "platform/common/thread_suspend_service.h"
#include <jni.h>
#include <string>
namespace artdcevm::thread_state_calibration {
// Ordinary Native JNI only. Observe the same actual current Thread before,
// inside, and after the independently calibrated JNI Runnable bridge. This
// never changes Thread state or calls a suspend/transition leaf. A successful
// passive legacy binding does not grant CanTransitionRunnable or source-stop.
bool Calibrate(HookEngine&,JNIEnv*,jobject strong_global,
               thread_suspend_service::Service*,std::string* error);
}
