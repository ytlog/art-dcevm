#include "platform/android13/evolution_transaction_abi.h"
#include "platform/android13/selective_deopt_abi.h"
#include "oat_quick_method_header.h"
#include "stack_map.h"
#include <cstring>
#include <new>
namespace fixed=artdcevm::android13::transaction_abi;
static_assert(sizeof(art::OatQuickMethodHeader)==sizeof(fixed::QuickHeader));
static_assert(fixed::kThreadInstrumentationStack==artdcevm::android13::selective_abi::kThreadInstrumentationStack);
extern "C" bool ArtDcevmSourceProbeEvolutionTransaction(bool(*inline_info)(const void*)) {
  if(!inline_info) return false;
  alignas(4) unsigned char bytes[64]{};
  auto* header=new(bytes+20) art::OatQuickMethodHeader(16);
  for(unsigned char value:{0u,1u,2u,3u}) {
    bytes[8]=value;
    if(!header->IsOptimized() || header->GetOptimizedCodeInfoPtr()!=bytes+8 ||
        inline_info(header)!=art::CodeInfo::HasInlineInfo(bytes+8)) return false;
  }
  // Unoptimized headers cannot have inline info even if preceding bytes do.
  uint32_t size=16;std::memcpy(header,&size,sizeof(size));
  return !header->IsOptimized() && !inline_info(header);
}
