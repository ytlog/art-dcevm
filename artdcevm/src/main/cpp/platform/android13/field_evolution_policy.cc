#include "platform/android13/field_evolution_policy.h"
#include <algorithm>

namespace artdcevm::android13 {
FieldEvolutionDecision CheckFieldEvolution(const std::vector<evolution::Member>& old_fields,
                                         const std::vector<evolution::Member>& new_fields) {
  constexpr uint32_t visibility = 0x0001 | 0x0002 | 0x0004;
  constexpr uint32_t final = 0x0010, volatile_flag = 0x0040;
  FieldEvolutionDecision decision;
  std::vector<bool> seen(old_fields.size(), false);
  for (const auto& field : new_fields) {
    auto old = std::find_if(old_fields.begin(), old_fields.end(), [&](const auto& candidate) {
      return candidate.name == field.name && candidate.descriptor == field.descriptor;
    });
    if (old == old_fields.end()) {
      decision.structural = true;
      continue;
    }
    const uint32_t difference = field.access ^ old->access;
    if (difference == 0) {
      seen[static_cast<size_t>(old - old_fields.begin())] = true;
      continue;
    }
    const bool visibility_change = (difference & visibility) != 0 &&
        (difference & ~(visibility | volatile_flag | final)) == 0;
    if (difference != evolution::kStatic && difference != volatile_flag &&
        difference != final && !visibility_change) {
      decision.accepted = false;
      decision.error = "Field " + field.name + ":" + field.descriptor +
          " had different access flags";
      return decision;
    }
    decision.structural = true;
    decision.final_guard |= (visibility_change || difference == final) &&
        (field.access & final) != 0;
  }
  decision.structural |= std::find(seen.begin(), seen.end(), false) != seen.end();
  return decision;
}
}
