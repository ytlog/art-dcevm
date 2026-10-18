#pragma once
#include "platform/abi/arm64/android11/class_definition.h"
#include "platform/abi/arm64/android12/class_definition.h"
#include "platform/abi/arm64/android13/class_definition.h"

namespace artdcevm::abi::arm64 {
constexpr const class_definition::Layout* ClassDefinitionsFor(int api) {
  if (api == 30) return &android11::kClassDefinitions;
  if (api == 31 || api == 32) return &android12::kClassDefinitions;
  if (api >= 33 && api <= 37) return &android13::kClassDefinitions;
  return nullptr;
}
}  // namespace artdcevm::abi::arm64
