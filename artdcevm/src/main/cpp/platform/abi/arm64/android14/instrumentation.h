#pragma once
#include <cstdint>
// runtime/instrumentation.h, fixed Android 14--17 prefix, arm64.
namespace artdcevm::abi::arm64::android14 {
struct Instrumentation {
  bool exit_hooks;
  int level;
  uint8_t forced, entry, exit, unwind, dex_pc;
};
}
