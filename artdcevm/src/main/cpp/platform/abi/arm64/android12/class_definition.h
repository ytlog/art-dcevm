#pragma once
#include "platform/abi/arm64/android11/class_definition.h"

// android-12.0.0_r1 and android-12.1.0_r1 retain the complete Android 11
// ArtClassDefinition and MemMap data declarations (API 31 and 32).
namespace artdcevm::abi::arm64::android12 {
using ArtClassDefinition = android11::ArtClassDefinition;
using ClassDefinitions = android11::ClassDefinitions;
inline constexpr auto kClassDefinitions = android11::kClassDefinitions;
}  // namespace artdcevm::abi::arm64::android12
