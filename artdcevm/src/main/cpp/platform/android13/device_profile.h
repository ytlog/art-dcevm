#pragma once
#include "support/elf_modules.h"
#include <array>
#include <string>

namespace artdcevm::android13 {
using ModuleFinder = LoadedModule (*)(const char* basename);

// Discovery of loaded source-family candidates, never proof of private layout.
// Bootstrap must observe public-JNI service receivers and finish all owned
// behavioral calibration before installing evolution hooks. Build IDs are
// retained in Modules solely for diagnostics, and never choose capabilities.
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

// Checks required loaded module basenames only. Failure resets output; no
// private object reads, device function calls, module loads or hooks happen here.
bool MatchDeviceProfile(DeviceProfile* output, std::string* error,
                        ModuleFinder finder = FindLoadedModule);
// Compiler experiments require a loaded compiler and named function probes.
// This does not enable or change any experiment or compiler option.
bool MatchCompilerDeviceProfile(const DeviceProfile& base, std::string* error,
                                ModuleFinder finder = FindLoadedModule);
}
