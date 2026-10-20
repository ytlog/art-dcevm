#pragma once
#include <cstdint>
// runtime/instrumentation.h, fixed Android 11 prefix, arm64.
namespace artdcevm::abi::arm64::android11 {
struct Instrumentation {
  uint64_t deopt_id;
  bool stubs, entry_stubs, interpreter_stubs, interpret_only, forced, entry, exit, unwind, dex_pc;
};
}
