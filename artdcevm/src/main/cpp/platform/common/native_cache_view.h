#pragma once
#include "platform/common/dex_cache_metadata.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android14/dex_cache.h"
namespace artdcevm::native_cache {
enum class Member { Fields, Methods };
struct View {
  void* pairs=nullptr;
  uint32_t pair_count=0;
  std::atomic<uintptr_t>* full=nullptr;
  uint32_t full_count=0;
};
// Borrow both allocated representations, rather than applying getter precedence.
// Caller supplies verified DEX ID counts for modern full storage and maintains
// calibrated ABI, mutator access and stable native allocations/publication.
// These checks prove arithmetic bounds only, not mapping extent or lifetime.
inline bool Read(dex_cache_metadata::Model model,const void* cache,size_t extent,Member member,uint32_t ids,View* output) {
  using Legacy=abi::arm64::android11::LegacyCache;
  using Dual=abi::arm64::android14::DualCache;
  if((model!=dex_cache_metadata::Model::Pairs && model!=dex_cache_metadata::Model::DualArrays) || !cache || !output ||
     (member!=Member::Fields && member!=Member::Methods))return false;
  const auto address=reinterpret_cast<uintptr_t>(cache);
  const size_t size=model==dex_cache_metadata::Model::Pairs?sizeof(Legacy):sizeof(Dual);
  if(address%8 || extent<size || address>UINTPTR_MAX-size)return false;
  const auto* bytes=static_cast<const unsigned char*>(cache);
  auto pointer=[&](size_t offset) {return __atomic_load_n(reinterpret_cast<const uint64_t*>(bytes+offset),__ATOMIC_ACQUIRE);};
  const bool fields=member==Member::Fields;
  View value;
  if(model==dex_cache_metadata::Model::Pairs) {
    value.pairs=reinterpret_cast<void*>(static_cast<uintptr_t>(pointer(fields?offsetof(Legacy,fields):offsetof(Legacy,methods))));
    const auto count=__atomic_load_n(reinterpret_cast<const uint32_t*>(bytes+(fields?offsetof(Legacy,num_fields):offsetof(Legacy,num_methods))),__ATOMIC_RELAXED);
    if(count>1024 || (!value.pairs && count))return false;
    value.pair_count=count;
  } else {
    value.pairs=reinterpret_cast<void*>(static_cast<uintptr_t>(pointer(fields?offsetof(Dual,fields):offsetof(Dual,methods))));
    value.pair_count=value.pairs?1024:0;
    value.full=reinterpret_cast<std::atomic<uintptr_t>*>(static_cast<uintptr_t>(pointer(fields?offsetof(Dual,fields_array):offsetof(Dual,methods_array))));
    value.full_count=value.full?ids:0;
  }
  const auto pairs=reinterpret_cast<uintptr_t>(value.pairs),full=reinterpret_cast<uintptr_t>(value.full);
  if((value.pairs && (pairs%16 || value.pair_count>(UINTPTR_MAX-pairs)/16)) ||
     (value.full && (full%8 || value.full_count>(UINTPTR_MAX-full)/8)))return false;
  *output=value;return true;
}
}
