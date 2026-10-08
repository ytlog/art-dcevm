#pragma once
#include <cstdint>
namespace artdcevm::code_info_flags {
// Scalar flag meanings, independent of CodeInfo object or serialized header ABI.
// A zero mask means this flag is not represented in this decoded flag word.
struct Schema {
  uint32_t inline_info, baseline, should_deoptimize, debuggable, fast;
};
}  // namespace artdcevm::code_info_flags
