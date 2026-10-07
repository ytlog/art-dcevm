#include "platform/android13/modifier_policy_binding.h"
#include "platform/android13/modifier_policy_abi.h"
#include "platform/android13/modifier_evolution_policy.h"
#include <dlfcn.h>

namespace artdcevm::android13 {
namespace {
int MethodPolicy(uint32_t old_flags, uint32_t new_flags, bool structural, bool new_direct) {
  return static_cast<int>(CheckMethodModifierEvolution(old_flags, new_flags, structural, new_direct));
}
int ClassPolicy(uint32_t old_flags, uint32_t new_flags, bool structural) {
  return static_cast<int>(CheckClassModifierEvolution(old_flags, new_flags, structural));
}
}
bool ConfigureEvolutionModifierPolicy(void* implementation, std::string* error) {
  auto configure = reinterpret_cast<ConfigureModifierPolicies>(
      dlsym(implementation, "ArtDcevmSourceConfigureModifierPolicies"));
  if (!configure) {
    *error = "Missing modifier policy adapter configuration";
    return false;
  }
  configure(MethodPolicy, ClassPolicy);
  return true;
}
}
