#include "platform/symbols/art_symbol_compat.h"
#include <cstdio>
#include "support/elf_symbol_names.h"

// Execute the production dependency enumeration, including generated JNI and
// reflection names. This engine records requests; it never loads ART or hooks.
class Inventory final : public artdcevm::HookEngine {
 public:
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* library, const char* symbol) override {
    std::printf("%s\t%s\n", library, symbol);
    return this;
  }
  void* Install(void*, void*, void**, std::string*) override { return nullptr; }
  bool Remove(void*, std::string*) override { return false; }
};

int main(int argc, char** argv) {
  if (argc == 2) {
    for (const auto& name : artdcevm::ReadUniqueElfSymbolNames(argv[1]))
      std::printf("%s\t%s\n", name.first.c_str(), name.second.c_str());
    return 0;
  }
  if (argc != 1) return 2;
  Inventory inventory;
  std::string error;
  return artdcevm::symbols::CheckEvolutionSymbols(inventory, &error) ? 0 : 1;
}
