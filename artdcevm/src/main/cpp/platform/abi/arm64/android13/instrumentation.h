#pragma once
#include <cstdint>
// runtime/instrumentation.h, fixed Android 12--13 prefix, arm64.
namespace artdcevm::abi::arm64::android13 {
struct Instrumentation {
  uint64_t deopt_id;
  bool stubs;
  int level;
  bool forced, entry, exit, unwind, dex_pc;
};
}
