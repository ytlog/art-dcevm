#pragma once
#include "platform/android13/device_profile.h"

#include "api/hook_engine.h"
#include <string>

namespace artdcevm::android13 {

bool InstallHierarchyResolutionHooks(HookEngine& hooks, const DeviceProfile& profile,
                                     std::string* error);

}  // namespace artdcevm::android13
