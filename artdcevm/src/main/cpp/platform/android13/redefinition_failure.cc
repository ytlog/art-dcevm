#include "platform/android13/redefinition_failure.h"
#include "platform/abi/arm64/android13/redefinition_art_abi.h"
#include "platform/abi/arm64/android13/platform_string_abi.h"
#include "platform/symbols/android13/source_symbols.h"
#include <cstring>

namespace artdcevm::android13 {
namespace {
using Failure = void (*)(void*, int, const void*, const BorrowedPlatformString*);
Failure failure = nullptr;
}
bool ConfigureRedefinitionFailures(HookEngine& hooks, std::string* error) {
  auto target = reinterpret_cast<Failure>(hooks.FindSymbol("libopenjdkjvmti.so", ::artdcevm::symbols::android13::kRecordFailure));
  if (!target) { *error = "Missing named ART redefinition error recorder"; return false; }
  failure = target;
  return true;
}
void RecordRedefinitionFailure(void* definition, int code, const char* message) {
  void* driver;
  std::memcpy(&driver, static_cast<unsigned char*>(definition) +
      offsetof(redefinition_abi::ClassRedefinition, driver), sizeof(driver));
  const auto* signature = static_cast<unsigned char*>(definition) +
      offsetof(redefinition_abi::ClassRedefinition, class_signature);
  // RecordFailure only synchronously reads c_str() from its const arguments.
  // The class signature remains platform-owned; no NDK string crosses this ABI.
  const BorrowedPlatformString text(message);
  failure(driver, code, signature, &text);
}
}
