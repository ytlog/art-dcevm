#pragma once
#include "platform/common/code_info_flag_schema.h"
#include "platform/abi/android11/code_info_flags.h"
#include "platform/abi/android14/code_info_flags.h"
#include "platform/abi/android15/code_info_flags.h"
#include "platform/abi/android17/code_info_flags.h"
#include <optional>
namespace artdcevm::code_info_flags {
struct State {
  bool inline_info, baseline;
  std::optional<bool> should_deoptimize, debuggable, fast;
  uint32_t unknown_bits;
};
inline State Decode(Schema schema, uint32_t decoded_flags) {
  auto flag = [&](uint32_t mask) -> std::optional<bool> {
    return mask ? std::optional<bool>((decoded_flags & mask) != 0) : std::nullopt;
  };
  return {(decoded_flags & schema.inline_info) != 0,
          (decoded_flags & schema.baseline) != 0,
          flag(schema.should_deoptimize), flag(schema.debuggable), flag(schema.fast),
          decoded_flags & ~(schema.inline_info | schema.baseline |
                            schema.should_deoptimize | schema.debuggable | schema.fast)};
}
// Selection is for fixed upstream source declarations only. API does not prove
// a device's Mainline/vendor ART matches this family. Device providers must pass
// their independently matched schema to Decode instead.
inline bool ReadSource(int api, uint32_t decoded_flags, State* result) {
  if (!result || api < 30 || api > 37) return false;
  Schema schema = code_info_flags_abi::android11::kSchema;
  if (api == 34) schema = code_info_flags_abi::android14::kSchema;
  else if (api == 35 || api == 36) schema = code_info_flags_abi::android15::kSchema;
  else if (api == 37) schema = code_info_flags_abi::android17::kSchema;
  *result = Decode(schema, decoded_flags);
  return true;
}
}  // namespace artdcevm::code_info_flags
