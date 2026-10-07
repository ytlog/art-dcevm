#include "platform/android13/device_profile.h"
#include <utility>

namespace artdcevm::android13 {
namespace {
struct Identity { const char* name; const char* build_id; };
// Actual DEVICE_001 arm64 release ELF notes, read with llvm-readelf on 2026-10-07.
// This table adds an exact identity restriction; source declaration/behavioral
// calibration remains mandatory in bootstrap and is not bypassed by a match.
constexpr Identity kBase[] = {
    {"libart.so", "14fc77b24db0b351b919c97009dc86bf"},
    {"libopenjdkjvmti.so", "2fe21c45581b9c8b0519b71cbde32938"},
    {"libdexfile.so", "e6aaffcef8ef63baf438267cda44d400"},
    {"libc++.so", "1f426797e505c9b841f55cc49d32b3f4"},
};
constexpr Identity kCompiler{
    "libart-compiler.so", "578147d9e59c98c6bc3c6faf9d27c9e6"};
constexpr char kDeclarationId[] = "android13-cas-arm64-release-baker-20261007";

bool Fail(std::string* error, std::string message) {
  if (error) *error = std::move(message);
  return false;
}
bool Check(const Identity& expected, const LoadedModule& actual, std::string* error) {
  if (actual.path.empty()) return Fail(error, std::string("Device profile missing loaded module: ") + expected.name);
  if (actual.build_id.empty()) return Fail(error, std::string("Device profile missing GNU Build ID: ") + expected.name);
  if (actual.build_id != expected.build_id)
    return Fail(error, std::string("Unsupported device profile for ") + expected.name +
        ": expected Build ID " + expected.build_id + ", loaded " + actual.build_id);
  return true;
}
}
const char* DeviceProfile::DeclarationId() const { return matched_ ? kDeclarationId : nullptr; }

bool MatchDeviceProfile(DeviceProfile* output, std::string* error, ModuleFinder finder) {
  if (output) *output = DeviceProfile{};
  if (error) error->clear();
  if (!output || !finder) return Fail(error, "Missing device profile output or module finder");
#if defined(__ANDROID__) && !defined(__aarch64__)
  return Fail(error, "Device profile requires Android arm64");
#endif
  DeviceProfile candidate;
  for (size_t i = 0; i < candidate.modules_.size(); ++i) {
    candidate.modules_[i] = finder(kBase[i].name);
    if (!Check(kBase[i], candidate.modules_[i], error)) return false;
  }
  candidate.matched_ = true;
  *output = std::move(candidate);
  return true;
}

bool MatchCompilerDeviceProfile(const DeviceProfile& base, std::string* error, ModuleFinder finder) {
  if (error) error->clear();
  if (!base.Matched()) return Fail(error, "Compiler profile requires a matched base device profile");
  if (!finder) return Fail(error, "Missing compiler profile module finder");
  return Check(kCompiler, finder(kCompiler.name), error);
}
}
