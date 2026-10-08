#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android11 {
// Fixed libartbase BitMemoryRegion/BitTable and runtime stack_map.h declarations.
// ARM64 native decoded objects, never the serialized CodeInfo header.
struct BitRegion { void* data; size_t bit_start, bit_size; };
template<size_t N> struct BitTable { BitRegion region; uint32_t rows; uint16_t columns[N+1]; };
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
static_assert(sizeof(BitTable<1>)==32 && sizeof(BitTable<8>)==48);
static_assert(sizeof(TableIterator)==16 && sizeof(InlineRange)==32 && sizeof(StackMapPair)==24);
static_assert(sizeof(CodeInfo)==328 && offsetof(CodeInfo,stack_maps)==24 && offsetof(CodeInfo,dex_regs)==16);
static_assert(sizeof(CodeInfoPair)==336);
// Fixed named native declarations; resolve only in an independently matched module.
using ConstructCodeInfo=void (*)(CodeInfo*,const void*);
using FindStackMap=TableIterator (*)(const CodeInfo*,uint32_t,int32_t);
inline constexpr const char* kConstructCodeInfo="_ZN3art8CodeInfoC2EPKNS_20OatQuickMethodHeaderE";
inline constexpr const char* kFindStackMap="_ZNK3art8CodeInfo28GetStackMapForNativePcOffsetEjNS_14InstructionSetE";

}
