#pragma once
#include <vector>
#include "platform/abi/arm64/common/redefinition.h"

namespace artdcevm::abi::arm64::android13 {
// openjdkjvmti/ti_redefine.cc, android-13.0.0_r1. The holder is private
// to the current transaction. libc++ ABI v1 vector<bool> matches ART's
// declaration; no ART object is constructed or destroyed here.
struct RedefinitionDataHolder {
  Handle array;
  void* redefinitions;
  std::vector<bool> initialized;
  std::vector<bool> actually_structural;
  std::vector<bool> initial_structural;
};
static_assert(sizeof(std::vector<bool>) == 24);
static_assert(offsetof(RedefinitionDataHolder, initialized) == 16);
}  // namespace artdcevm::abi::arm64::android13
