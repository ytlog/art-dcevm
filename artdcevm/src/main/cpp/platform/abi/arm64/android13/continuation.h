#pragma once
#include "platform/abi/arm64/android13/shadow_frame.h"
namespace artdcevm::abi::arm64::android13 {
// libdexfile/dex/code_item_accessors.h, android-13.0.0_r1, arm64.
struct CodeItemDataAccessor {
  uint32_t size;
  const uint16_t* insns;
  uint16_t registers, ins, outs, tries;
};
static_assert(offsetof(CodeItemDataAccessor, insns) == 8);
}
