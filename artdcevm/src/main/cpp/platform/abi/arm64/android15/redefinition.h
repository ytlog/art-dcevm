#pragma once
#include <vector>
#include "platform/abi/arm64/common/redefinition.h"

namespace artdcevm::abi::arm64::android15 {
// ti_redefine.h: 3 trailing bools, last has_virtuals_.
using ClassRedefinition = ClassRedefinitionLayout<3>;
static_assert(sizeof(ClassRedefinition) == 72);
static_assert(offsetof(ClassRedefinition, original_dex_file) == 48);
// ti_redefine.cc: unchanged transaction-private holder prefix and flag vectors.
struct RedefinitionDataHolder {
  using Redefinition = ClassRedefinition;
  Handle array;
  void* redefinitions;
  std::vector<bool> initialized, actually_structural, initial_structural;
};
static_assert(sizeof(std::vector<bool>) == 24);
static_assert(offsetof(RedefinitionDataHolder, initialized) == 16);
static_assert(offsetof(RedefinitionDataHolder, actually_structural) == 40);
static_assert(offsetof(RedefinitionDataHolder, initial_structural) == 64);
}  // namespace artdcevm::abi::arm64::android15
