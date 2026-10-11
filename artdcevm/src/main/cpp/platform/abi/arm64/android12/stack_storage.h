#pragma once
#include "platform/abi/arm64/android11/stack_storage.h"
namespace artdcevm::abi::arm64::android12 {
using BitRegion=android11::BitRegion;
// Both Android11 and Android12/12L retain size_t num_rows_.
// https://android.googlesource.com/platform/art/+/refs/tags/android-12.1.0_r27/libartbase/base/bit_table.h
template<size_t N> using BitTable=android11::BitTable<N>;
static_assert(offsetof(BitTable<1>,rows)==24 && offsetof(BitTable<1>,columns)==32);
using TableIterator=android11::TableIterator;
using InlineRange=android11::InlineRange;
using StackMapPair=android11::StackMapPair;
// Fixed Android 12--12.1 CodeInfo native decoded object.
struct CodeInfo {
  uint32_t flags, code_size, frame_size, core_mask, fp_mask, dex_regs, table_flags;
  BitTable<8> stack_maps; BitTable<2> register_masks; BitTable<1> stack_masks;
  BitTable<6> inline_infos; BitTable<1> method_infos;
  BitTable<1> dex_masks, dex_maps; BitTable<2> dex_catalog;
};
struct CodeInfoPair { void* header; CodeInfo info; };
static_assert(sizeof(CodeInfo)==376 && offsetof(CodeInfo,stack_maps)==32 && offsetof(CodeInfo,dex_regs)==20);
static_assert(sizeof(CodeInfoPair)==384);
}
