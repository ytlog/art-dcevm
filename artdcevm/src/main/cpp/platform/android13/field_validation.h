#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"

namespace artdcevm::android13 {
bool ConfigureEvolutionFieldValidation(HookEngine& hooks, const DeviceProfile& profile, std::string* error);
bool CheckEvolutionFields(void* definition);
}
