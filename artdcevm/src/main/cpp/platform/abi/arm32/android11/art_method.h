#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm32::android11 {
// android-11.0.0_r1 runtime/art_method.h: image PointerSize::k32.
// HeapReference<Class> remains 32 bits; native pointers are 32 bits too.
struct Method11 {
  uint32_t declaring_class, access_flags, dex_code_item_offset, dex_method_index;
  uint16_t method_index, hotness_count;
  uint32_t data, entry_point;
};
static_assert(sizeof(Method11)==28 && alignof(Method11)==4);
static_assert(offsetof(Method11,data)==20 && offsetof(Method11,entry_point)==24);
}
