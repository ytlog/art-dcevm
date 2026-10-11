#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android11 {
// Fixed libartbase BitMemoryRegion/BitTable and runtime stack_map.h declarations.
// ARM64 native decoded objects, never the serialized CodeInfo header.
struct BitRegion { void* data; size_t bit_start, bit_size; };
// Fixed Android11/12 libartbase/base/bit_table.h retains size_t num_rows_.
// Android13 switches to uint32_t and has a separate declaration.
// https://android.googlesource.com/platform/art/+/refs/tags/android-11.0.0_r1/libartbase/base/bit_table.h
template<size_t N> struct BitTable { BitRegion region; size_t rows; uint16_t columns[N+1]; };
struct TableIterator { void* table; uint32_t row; };
struct InlineRange { TableIterator begin, end; };
struct StackMapPair { uintptr_t pc; TableIterator map; };
struct CodeInfo {
  uint32_t flags, frame_size, core_mask, fp_mask, dex_regs, table_flags;
  BitTable<8> stack_maps; BitTable<2> register_masks; BitTable<1> stack_masks;
  BitTable<6> inline_infos; BitTable<1> method_infos;
  BitTable<1> dex_masks, dex_maps; BitTable<2> dex_catalog;
};
struct CodeInfoPair { void* header; CodeInfo info; };
static_assert(sizeof(void*)==8 && sizeof(size_t)==8);
static_assert(sizeof(BitRegion)==24 && alignof(BitRegion)==8);
static_assert(sizeof(BitTable<1>)==40 && sizeof(BitTable<8>)==56);
static_assert(sizeof(TableIterator)==16 && sizeof(InlineRange)==32 && sizeof(StackMapPair)==24);
static_assert(sizeof(CodeInfo)==368 && offsetof(CodeInfo,stack_maps)==24 && offsetof(CodeInfo,dex_regs)==16);
static_assert(sizeof(CodeInfoPair)==376);
}
