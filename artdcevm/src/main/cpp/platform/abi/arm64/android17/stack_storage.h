#pragma once
#include "platform/abi/arm64/android11/stack_storage.h"
namespace artdcevm::abi::arm64::android17 {
using BitRegion=android11::BitRegion;
// Android 17 BitTableBase stores the final row end separately as uint32_t.
template<size_t N> struct BitTable { BitRegion region; uint32_t rows, row_bits; uint16_t columns[N]; };
using TableIterator=android11::TableIterator;
using InlineRange=android11::InlineRange;
using StackMapPair=android11::StackMapPair;
struct CodeInfo {
  uint32_t flags, code_size, frame_size, core_mask, fp_mask, dex_regs, table_flags;
  BitTable<8> stack_maps; BitTable<2> register_masks; BitTable<1> stack_masks;
  BitTable<6> inline_infos; BitTable<3> method_infos;
  BitTable<1> dex_masks, dex_maps; BitTable<2> dex_catalog;
};
struct CodeInfoPair { void* header; CodeInfo info; };
static_assert(sizeof(BitTable<1>)==40 && sizeof(BitTable<8>)==48);
static_assert(offsetof(BitTable<1>,row_bits)==28 && offsetof(BitTable<1>,columns)==32);
static_assert(sizeof(CodeInfo)==368 && offsetof(CodeInfo,stack_maps)==32 && offsetof(CodeInfo,dex_regs)==20);
static_assert(offsetof(CodeInfo,dex_masks)==248 && sizeof(CodeInfoPair)==376);
}
