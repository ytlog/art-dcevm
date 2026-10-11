#pragma once
#include "evolution/class_schema.h"
#include <array>
namespace artdcevm::ordinary_source_fixture {
inline constexpr const char* kDescriptor="Lcom/github/ytlog/artdcevm/internal/OrdinarySourceFixture;";
// Independent installed/public declaration witness. Every step adds one
// fixture method through the original public structural extension, retaining
// its fields, constructor and compute oracle. No private deletion is needed.
bool Validate(const evolution::ClassSchema& live,
    const std::array<std::vector<unsigned char>,3>& dex,std::string* error);
}
