#pragma once

#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <string>

namespace artdcevm::android13 {
void* CanonicalizeJniMethod(void*);
bool ConfigureJniMethodTargets(HookEngine& hooks, const DeviceProfile& profile, std::string* error);
}
