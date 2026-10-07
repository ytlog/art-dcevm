#include "platform/symbols/art_symbol_compat.h"
#include <cstdio>

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

int main() {
  Inventory inventory;
  std::string error;
  return artdcevm::symbols::CheckEvolutionSymbols(inventory, &error) ? 0 : 1;
}
