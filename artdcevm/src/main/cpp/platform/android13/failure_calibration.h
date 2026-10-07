#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
namespace artdcevm::android13 {
// Uses a local empty transaction; never mutates a live class or owns JNI roots.
// Must succeed before replacing the old field-validation failure probe.
bool CalibrateRedefinitionFailures(HookEngine&,const DeviceProfile&,std::string* error);
}
