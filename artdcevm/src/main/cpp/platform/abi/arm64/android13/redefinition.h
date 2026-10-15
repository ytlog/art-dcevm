#pragma once
#include <vector>
#include "platform/abi/arm64/common/redefinition.h"

namespace artdcevm::abi::arm64::android13 {
// openjdkjvmti/ti_redefine.cc, android-13.0.0_r1. The holder is private
// to the current transaction. libc++ ABI v1 vector<bool> matches ART's
// declaration; no ART object is constructed or destroyed here.
// ti_redefine.h: added_fields_, added_methods_, has_virtuals_, lock_acquired_.
using ClassRedefinition = ClassRedefinitionLayout<4>;
static_assert(sizeof(ClassRedefinition) == 72);
static_assert(offsetof(ClassRedefinition, original_dex_file) == 48);
struct RedefinitionDataHolder {
  using Redefinition = ClassRedefinition;
  Handle array;
  void* redefinitions;
  std::vector<bool> initialized;
  std::vector<bool> actually_structural;
  std::vector<bool> initial_structural;
};
static_assert(sizeof(std::vector<bool>) == 24);
static_assert(offsetof(RedefinitionDataHolder, initialized) == 16);
}  // namespace artdcevm::abi::arm64::android13
