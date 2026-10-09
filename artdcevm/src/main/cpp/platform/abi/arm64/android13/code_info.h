#pragma once
#include "platform/abi/arm64/android11/code_info.h"
namespace artdcevm::abi::arm64::android13 {
// Fixed stack_map.h CodeInfo::kHasInlineInfo (positive native WalkStack oracle only).
inline constexpr uint32_t kHasInlineInfo=1u<<0;
using BitRegion=android11::BitRegion;
template<size_t N> struct BitTable { BitRegion region; uint32_t rows; uint16_t columns[N+1]; };
static_assert(sizeof(BitTable<1>)==32 && sizeof(BitTable<8>)==48);
using TableIterator=android11::TableIterator;
using InlineRange=android11::InlineRange;
using StackMapPair=android11::StackMapPair;
// Fixed Android 13--16 CodeInfo native decoded object.
struct CodeInfo {
  uint32_t flags, code_size, frame_size, core_mask, fp_mask, dex_regs, table_flags;
  BitTable<8> stack_maps; BitTable<2> register_masks; BitTable<1> stack_masks;
  BitTable<6> inline_infos; BitTable<3> method_infos;
  BitTable<1> dex_masks, dex_maps; BitTable<2> dex_catalog;
};
struct CodeInfoPair { void* header; CodeInfo info; };
static_assert(sizeof(CodeInfo)==344 && offsetof(CodeInfo,stack_maps)==32 && offsetof(CodeInfo,dex_regs)==20);
static_assert(sizeof(CodeInfoPair)==352);
// Fixed named native declarations; resolve only in an independently matched module.
using ConstructCodeInfo=void (*)(CodeInfo*,const void*);
using FindStackMap=TableIterator (*)(const CodeInfo*,uintptr_t,int32_t);
inline constexpr const char* kConstructCodeInfo="_ZN3art8CodeInfoC2EPKNS_20OatQuickMethodHeaderE";
inline constexpr const char* kFindStackMap="_ZNK3art8CodeInfo28GetStackMapForNativePcOffsetEmNS_14InstructionSetE";

}
