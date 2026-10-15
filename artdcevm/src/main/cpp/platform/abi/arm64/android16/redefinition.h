#pragma once
#include <vector>
#include "platform/abi/arm64/common/redefinition.h"
namespace artdcevm::abi::arm64::android16 {
// openjdkjvmti/ti_redefine.cc, android-16.0.0_r1; private current transaction.
struct RedefinitionDataHolder {
  Handle array;
  void* redefinitions;
  std::vector<bool> initialized, actually_structural, initial_structural;
};
static_assert(sizeof(std::vector<bool>) == 24);
static_assert(offsetof(RedefinitionDataHolder, initialized) == 16);
}  // namespace artdcevm::abi::arm64::android16
