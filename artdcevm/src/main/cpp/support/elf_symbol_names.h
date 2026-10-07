#pragma once
#include <string>
#include <unordered_map>

namespace artdcevm {
// Read names only, never symbol values or instruction bytes. Ambiguous compiler
// unique names are omitted; exact ELF lookup remains the first choice.
std::unordered_map<std::string, std::string> ReadUniqueElfSymbolNames(const std::string& path);
}
