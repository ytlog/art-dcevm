#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace artdcevm::abi::arm64::android11 {
// Fixed android-11.0.0_r1 runtime/oat_quick_method_header.h, ARM64.
// Inline Contains/GetCodeSize have no required out-of-line ELF body.
struct OatQuickMethodHeaderPrefix { uint32_t vmap_table_offset,code_size; };
static_assert(sizeof(OatQuickMethodHeaderPrefix)==8 && alignof(OatQuickMethodHeaderPrefix)==4);
static_assert(offsetof(OatQuickMethodHeaderPrefix,vmap_table_offset)==0 &&
              offsetof(OatQuickMethodHeaderPrefix,code_size)==4);
inline bool OptimizedHeaderContains(const void* header,size_t readable_extent,uintptr_t pc) {
  if(!header || readable_extent<sizeof(OatQuickMethodHeaderPrefix))return false;
  const uintptr_t address=reinterpret_cast<uintptr_t>(header);
  if(address>UINTPTR_MAX-sizeof(OatQuickMethodHeaderPrefix))return false;
  OatQuickMethodHeaderPrefix prefix;
  std::memcpy(&prefix,header,sizeof(prefix));
  // GetCodeSize rejects assembly stub sentinels; IsOptimized also requires a
  // nonzero vmap displacement. The high bit is the source deoptimization flag.
  if(prefix.code_size==UINT32_MAX || !prefix.vmap_table_offset)return false;
  const uint32_t size=prefix.code_size&0x7fffffffu;
  const uintptr_t code=address+sizeof(prefix);
  if(!size || prefix.vmap_table_offset>code || code>UINTPTR_MAX-size)return false;
  return code<=pc && pc<=code+size;
}
}
