#pragma once
#include "platform/abi/arm64/android11/class_redefinition.h"
#include "platform/abi/arm64/android13/class_redefinition.h"
#include "platform/abi/arm64/android15/class_redefinition.h"

namespace artdcevm::abi::arm64::android_common {
// The source declarations for 11--12, 13--14 and 15--17 share the
// fields used by the common transaction adapter. Their trailing policy
// byte differs and must remain outside this shared adapter's contract.
using ClassRedefinitionPrefix = android15::ClassRedefinition;
#define ARTDCEVM_SHARED_REDEFINITION(Family) \
  static_assert(sizeof(Family::ClassRedefinition) == sizeof(ClassRedefinitionPrefix)); \
  static_assert(offsetof(Family::ClassRedefinition, driver) == offsetof(ClassRedefinitionPrefix, driver)); \
  static_assert(offsetof(Family::ClassRedefinition, klass) == offsetof(ClassRedefinitionPrefix, klass)); \
  static_assert(offsetof(Family::ClassRedefinition, dex_file) == offsetof(ClassRedefinitionPrefix, dex_file)); \
  static_assert(offsetof(Family::ClassRedefinition, class_signature) == offsetof(ClassRedefinitionPrefix, class_signature)); \
  static_assert(offsetof(Family::ClassRedefinition, original_dex_file) == offsetof(ClassRedefinitionPrefix, original_dex_file)); \
  static_assert(offsetof(Family::ClassRedefinition, added_fields) == offsetof(ClassRedefinitionPrefix, added_fields)); \
  static_assert(offsetof(Family::ClassRedefinition, added_methods) == offsetof(ClassRedefinitionPrefix, added_methods)); \
  static_assert(offsetof(Family::ClassRedefinition, has_virtuals) == offsetof(ClassRedefinitionPrefix, has_virtuals))
ARTDCEVM_SHARED_REDEFINITION(android11);
ARTDCEVM_SHARED_REDEFINITION(android13);
#undef ARTDCEVM_SHARED_REDEFINITION
}  // namespace artdcevm::abi::arm64::android_common
