#pragma once
#include <cstdint>

namespace artdcevm::android13 {
using MethodModifierPolicy = int (*)(uint32_t, uint32_t, bool, bool);
using ClassModifierPolicy = int (*)(uint32_t, uint32_t, bool);
using ConfigureModifierPolicies = void (*)(MethodModifierPolicy, ClassModifierPolicy);
}
extern "C" int ArtDcevmSourceMethodModifierPolicy(uint32_t old_flags, uint32_t new_flags,
                                                bool structural, bool new_direct);
extern "C" int ArtDcevmSourceClassModifierPolicy(uint32_t old_flags, uint32_t new_flags,
                                               bool structural);
