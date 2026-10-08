#pragma once
#include "platform/abi/arm64/android11/code_info.h"
namespace artdcevm::abi::arm64::android12 {
using BitRegion=android11::BitRegion;
template<size_t N> using BitTable=android11::BitTable<N>;
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
static_assert(sizeof(CodeInfo)==336 && offsetof(CodeInfo,stack_maps)==32 && offsetof(CodeInfo,dex_regs)==20);
static_assert(sizeof(CodeInfoPair)==344);
// Fixed named native declarations; resolve only in an independently matched module.
using ConstructCodeInfo=void (*)(CodeInfo*,const void*);
using FindStackMap=TableIterator (*)(const CodeInfo*,uintptr_t,int32_t);
inline constexpr const char* kConstructCodeInfo="_ZN3art8CodeInfoC2EPKNS_20OatQuickMethodHeaderE";
inline constexpr const char* kFindStackMap="_ZNK3art8CodeInfo28GetStackMapForNativePcOffsetEmNS_14InstructionSetE";

}
