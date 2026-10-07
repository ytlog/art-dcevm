#pragma once
#include <cstdint>

namespace artdcevm::android13 {
// Plain integer ABI: source adapters can request a runtime guard without moving
// platform C++ objects or policy implementations across the library boundary.
enum class ModifierEvolutionAction : int {
  kReject = 0,
  kUnchanged = 1,
  kRecreate = 2,
  kCheckFinalOverride = 3,
  kCheckAbstractDependents = 4,
  kCheckLoadedSubclass = 5,
};
ModifierEvolutionAction CheckMethodModifierEvolution(uint32_t old_flags, uint32_t new_flags,
                                                     bool structural, bool new_direct);
ModifierEvolutionAction CheckClassModifierEvolution(uint32_t old_flags, uint32_t new_flags,
                                                    bool structural);
}
