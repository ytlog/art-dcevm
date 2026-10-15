#pragma once
#include <vector>
#include "platform/abi/arm64/common/redefinition.h"
namespace artdcevm::abi::arm64::android16 {
// openjdkjvmti/ti_redefine.cc, android-16.0.0_r1; private current transaction.
// ti_redefine.h: added_fields_, added_methods_, has_virtuals_.
using ClassRedefinition = ClassRedefinitionLayout<3>;
static_assert(sizeof(ClassRedefinition) == 72);
static_assert(offsetof(ClassRedefinition, original_dex_file) == 48);
struct RedefinitionDataHolder {
  using Redefinition = ClassRedefinition;
  Handle array;
  void* redefinitions;
  std::vector<bool> initialized, actually_structural, initial_structural;
};
static_assert(sizeof(std::vector<bool>) == 24);
static_assert(offsetof(RedefinitionDataHolder, initialized) == 16);
}  // namespace artdcevm::abi::arm64::android16
