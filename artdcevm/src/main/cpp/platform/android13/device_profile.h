#pragma once
#include "support/elf_modules.h"
#include <array>
#include <string>

namespace artdcevm::android13 {
using ModuleFinder = LoadedModule (*)(const char* basename);

// Identity of the locked declaration set, not permission to skip behavioral
// calibration. Bootstrap must finish owned calibration before installing hooks.
class DeviceProfile {
 public:
  bool Matched() const { return matched_; }
  const char* DeclarationId() const;
  const std::array<LoadedModule, 4>& Modules() const { return modules_; }
 private:
  bool matched_ = false;
  std::array<LoadedModule, 4> modules_{};
  friend bool MatchDeviceProfile(DeviceProfile*, std::string*, ModuleFinder);
};

// Reads loaded ELF module identities only. Failure resets output; there are no
// private object reads, device function calls, module loads or hooks here.
bool MatchDeviceProfile(DeviceProfile* output, std::string* error,
                        ModuleFinder finder = FindLoadedModule);
// Compiler experiments require an independently matched compiler module.
// This does not enable or change any experiment or compiler option.
bool MatchCompilerDeviceProfile(const DeviceProfile& base, std::string* error,
                                ModuleFinder finder = FindLoadedModule);
}
