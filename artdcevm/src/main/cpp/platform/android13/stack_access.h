#pragma once
#include "platform/android13/device_profile.h"
#include "api/hook_engine.h"
#include "platform/abi/arm64/android13/stack_access_abi.h"
#include <string>
namespace artdcevm::android13 {
bool ConfigureStackAccess(HookEngine& hooks, const DeviceProfile& profile, std::string* error);
}
