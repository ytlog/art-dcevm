#pragma once

#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <string>
#include <cstdint>

namespace artdcevm::android13 {

void SetAsyncDeoptProbe(bool enabled);
uint64_t AsyncDeoptProbeHits();

bool InstallAsyncDeoptHook(HookEngine& hooks, const DeviceProfile& profile,
                           std::string* error);

}  // namespace artdcevm::android13
