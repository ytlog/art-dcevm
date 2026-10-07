#pragma once
#include "evolution/class_schema.h"

namespace artdcevm::android13 {
struct FieldEvolutionDecision {
  bool accepted = true;
  bool structural = false;
  bool final_guard = false;
  std::string error;
};
FieldEvolutionDecision CheckFieldEvolution(const std::vector<evolution::Member>& old_fields,
                                         const std::vector<evolution::Member>& new_fields);
}
