#pragma once
#include <cstdint>
#include <cstddef>
namespace artdcevm::abi::arm64::android11 {
// Fixed android-11.0.0_r1/runtime/oat_quick_method_header.h. Returned by the
// actual native StackVisitor, never located by disassembly or address guessing.
struct QuickMethodHeader {uint32_t vmap_table_offset,code_size;};
static_assert(sizeof(QuickMethodHeader)==8&&offsetof(QuickMethodHeader,code_size)==4);
inline constexpr uint32_t kShouldDeoptimizeMask=0x80000000u;
}
