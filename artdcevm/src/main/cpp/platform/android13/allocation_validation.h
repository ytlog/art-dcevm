#pragma once
#include "platform/android13/device_profile.h"
#include "platform/android13/allocation_validation_abi.h"
#include <string>
namespace artdcevm::android13 {
using CollectInstances = bool (*)(void*, void*);
bool ConfigureAllocationValidation(const DeviceProfile& profile, std::string* error);
bool ReadAllocationState(void* data, allocation_abi::State* state);
// Called only after successful device allocation. The holder owns the roots;
// this checker allocates nothing and preserves an existing allocation failure.
bool ValidateAllocatedInstances(void* definition, void* data);
bool CollectAndValidateInstances(bool evolution, void* definition, void* data,
                                 CollectInstances original);
}
