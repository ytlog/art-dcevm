#include "platform/android13/instance_allocation_hooks.h"
#include "platform/android13/allocation_validation.h"
#include "platform/android13/validation_hooks.h"
#include "platform/android13/symbols/profile.h"

namespace artdcevm::android13 {
namespace {
CollectInstances original_collect = nullptr;
bool CollectProxy(void* definition, void* data) {
  return CollectAndValidateInstances(ValidationHooks::InSourceTransaction(),
                                     definition, data, original_collect);
}
}
bool InstallInstanceAllocationHook(HookEngine& hooks, const DeviceProfile& profile,
                                   std::string* error) {
  if (!ConfigureAllocationValidation(profile, error)) return false;
  void* target = hooks.FindSymbol("libopenjdkjvmti.so", cas::kCollectNewInstances);
  if (!target) { *error = "Missing named device instance-allocation function"; return false; }
  return hooks.Install(target, reinterpret_cast<void*>(CollectProxy),
                        reinterpret_cast<void**>(&original_collect), error) != nullptr;
}
}
