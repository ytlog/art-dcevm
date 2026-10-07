#pragma once
#include "platform/android13/device_profile.h"
#include "api/hook_engine.h"
namespace artdcevm::android13 {
bool ConfigureEvolutionClassValidation(HookEngine&, const DeviceProfile& profile, std::string* error);
bool CheckEvolutionClass(void* definition);
}
