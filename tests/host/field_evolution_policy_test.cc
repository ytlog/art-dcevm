#include "platform/android13/field_evolution_policy.h"
#include <cassert>

using artdcevm::android13::CheckFieldEvolution;
using artdcevm::evolution::Member;

int main() {
  const Member original{"value", "I", 1, 0};
  auto unchanged = CheckFieldEvolution({original}, {original});
  assert(unchanged.accepted && !unchanged.structural && !unchanged.final_guard);
  auto removed = CheckFieldEvolution({original}, {});
  assert(removed.accepted && removed.structural);
  auto type_change = CheckFieldEvolution({original}, {{"value", "J", 1, 0}});
  assert(type_change.accepted && type_change.structural);
  auto storage_change = CheckFieldEvolution({original}, {{"value", "I", 9, 0}});
  assert(storage_change.accepted && storage_change.structural && !storage_change.final_guard);
  auto add_final = CheckFieldEvolution({original}, {{"value", "I", 0x11, 0}});
  assert(add_final.accepted && add_final.structural && add_final.final_guard);
  auto remove_final = CheckFieldEvolution({{"value", "I", 0x11, 0}}, {original});
  assert(remove_final.accepted && remove_final.structural && !remove_final.final_guard);
  auto volatile_change = CheckFieldEvolution({original}, {{"value", "I", 0x41, 0}});
  assert(volatile_change.accepted && volatile_change.structural && !volatile_change.final_guard);
  auto remove_volatile = CheckFieldEvolution({{"value", "I", 0x41, 0}}, {original});
  assert(remove_volatile.accepted && remove_volatile.structural && !remove_volatile.final_guard);
  auto visibility_and_final = CheckFieldEvolution({original}, {{"value", "I", 0x14, 0}});
  assert(visibility_and_final.accepted && visibility_and_final.final_guard);
  auto visibility_and_volatile = CheckFieldEvolution({original}, {{"value", "I", 0x44, 0}});
  assert(visibility_and_volatile.accepted && !visibility_and_volatile.final_guard);
  auto private_to_package = CheckFieldEvolution({{"value", "I", 2, 0}}, {{"value", "I", 0, 0}});
  assert(private_to_package.accepted && private_to_package.structural);
  // Existing policy does not admit unrelated modifier combinations. Rejection
  // must precede transaction mutation; it cannot be treated as a field addition.
  for (uint32_t access : {0x19u, 0x51u, 0x1001u}) {
    auto unsupported = CheckFieldEvolution({original}, {{"value", "I", access, 0}});
    assert(!unsupported.accepted && !unsupported.error.empty());
  }
  auto new_final = CheckFieldEvolution({}, {{"added", "I", 0x11, 0}});
  assert(new_final.accepted && new_final.structural && !new_final.final_guard);
  // Reordered declarations keep identity and require no storage migration.
  const Member second{"wide", "J", 9, 1};
  auto reordered = CheckFieldEvolution({original, second}, {second, original});
  assert(reordered.accepted && !reordered.structural);
}
