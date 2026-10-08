#pragma once
#include <cstdint>
namespace artdcevm::abi::arm64::android12 {
// Fixed runtime/art_method.h declaration; later versions may reuse this family.
struct Method12 {
  uint32_t declaring_class,access_flags,dex_method_index;
  uint16_t method_index,hotness_count;
  uint64_t data,entry_point;
};
}
