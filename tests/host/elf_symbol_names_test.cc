#include "support/elf_symbol_names.h"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <map>
#include "fixtures/android14_checkjni_names.h"
int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) return 2;
  const auto names = artdcevm::ReadUniqueElfSymbolNames(argv[1]);
  if (argc == 3) {
    if (!std::strcmp(argv[2], "compressed")) {
      assert(names.size() == 3);
      assert(names.at("_Zlto") == "_Zlto.__uniq.77.llvm.88");
      assert(names.at("_Zlocal") == "_Zlocal.__uniq.123");
      assert(names.at("_Zglobal") == "_Zglobal.__uniq.4");
      assert(!names.count("_Zambiguous"));
    } else if (!std::strcmp(argv[2], "checkjni")) {
      assert(names.size() == 36);
      for (const char* full : kAndroid14CheckJniNames) {
        const std::string observed(full);
        const auto at = observed.find(".__uniq.");
        assert(at != std::string::npos);
        assert(names.at(observed.substr(0, at)) == observed);
      }
    } else if (!std::strcmp(argv[2], "reject")) {
      assert(names.empty());
    } else return 2;
  }
  for (const auto& name : std::map<std::string, std::string>(names.begin(), names.end()))
    std::printf("%s\t%s\n", name.first.c_str(), name.second.c_str());
}
