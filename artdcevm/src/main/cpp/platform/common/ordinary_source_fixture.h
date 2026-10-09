#pragma once
#include "evolution/class_schema.h"
#include <array>
namespace artdcevm::ordinary_source_fixture {
inline constexpr const char* kDescriptor="Lcom/github/ytlog/artdcevm/internal/OrdinarySourceFixture;";
// Independent installed/public declaration witness. Every step removes one
// unused fixture method, retaining its field, constructor and compute oracle.
bool Validate(const evolution::ClassSchema& live,
    const std::array<std::vector<unsigned char>,3>& dex,std::string* error);
}
