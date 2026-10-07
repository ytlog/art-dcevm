#pragma once
#include "platform/android13/device_profile.h"
#include "api/hook_engine.h"
#include <string>
namespace artdcevm::android13 {
bool ConfigureSelectiveDeopt(HookEngine&,const DeviceProfile& profile,std::string*);
void DeoptimizeApplicationFrames();
}
