#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13::transaction_abi {
// runtime/oat_quick_method_header.h and stack_map.h, locked release profile.
struct QuickHeader { uint32_t data; };
inline constexpr uint32_t kCodeInfo=0x40000000,kCodeInfoOffset=0x3fffffff;
inline constexpr uint8_t kHasInlineInfo=1;
inline constexpr size_t kThreadInstrumentationStack=304;
static_assert(sizeof(QuickHeader)==4);
}
