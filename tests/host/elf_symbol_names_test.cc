#include "support/elf_symbol_names.h"
#include <cstdio>
#include <map>
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const auto names = artdcevm::ReadUniqueElfSymbolNames(argv[1]);
  for (const auto& name : std::map<std::string, std::string>(names.begin(), names.end()))
    std::printf("%s\t%s\n", name.first.c_str(), name.second.c_str());
}
