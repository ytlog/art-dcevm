#include "platform/android13/modifier_policy_abi.h"
#include "platform/android13/modifier_evolution_policy.h"
namespace {
artdcevm::android13::MethodModifierPolicy method_policy = nullptr;
artdcevm::android13::ClassModifierPolicy class_policy = nullptr;
}
extern "C" void ArtDcevmSourceConfigureModifierPolicies(
    artdcevm::android13::MethodModifierPolicy methods,
    artdcevm::android13::ClassModifierPolicy classes) {
  method_policy = methods;
  class_policy = classes;
}
extern "C" int ArtDcevmSourceMethodModifierPolicy(uint32_t old_flags, uint32_t new_flags,
                                                bool structural, bool new_direct) {
  return method_policy ? method_policy(old_flags, new_flags, structural, new_direct)
      : static_cast<int>(artdcevm::android13::ModifierEvolutionAction::kReject);
}
extern "C" int ArtDcevmSourceClassModifierPolicy(uint32_t old_flags, uint32_t new_flags,
                                               bool structural) {
  return class_policy ? class_policy(old_flags, new_flags, structural)
      : static_cast<int>(artdcevm::android13::ModifierEvolutionAction::kReject);
}
