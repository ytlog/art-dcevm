#pragma once
#include "platform/abi/arm64/android11/gc_cache_pair.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android14/dex_cache.h"
#include <cstring>
namespace artdcevm::type_cache {
using Pair=abi::arm64::android11::GcCachePair;
using Legacy=abi::arm64::android11::LegacyCache;
using Dual=abi::arm64::android14::DualCache;
struct View {
  Pair* pairs=nullptr;
  uint32_t pair_count=0;
  uint32_t* full=nullptr;
  uint32_t full_count=0;
  // Callback receives a local opaque root slot for the provider's named
  // barrier, updates it and returns true only for an intended replacement.
  // No suspension/GC/unlink may occur while this borrowed storage is used.
  // Exclusive writer access is required to avoid losing a concurrent setter.
  // The caller performs the DexCache write barrier after this operation.
  template<class Transform> uint64_t Apply(Transform transform) const {
    uint64_t changed=0;
    for(uint32_t i=0;i<pair_count;++i) {
      Pair value;__atomic_load(pairs+i,&value,__ATOMIC_RELAXED);
      if(transform(&value.object)) {
        __atomic_store(pairs+i,&value,__ATOMIC_RELEASE);++changed;
      }
    }
    for(uint32_t i=0;i<full_count;++i) {
      uint32_t value=__atomic_load_n(full+i,__ATOMIC_RELAXED);
      if(transform(&value)) {__atomic_store_n(full+i,value,__ATOMIC_RELAXED);++changed;}
    }
    return changed;
  }
};
// Exact device ABI, rooted live cache and stable ART-owned native allocations
// are prerequisites. Checks prove arithmetic bounds, not independent mapping
// extents. Both allocated representations need updates, regardless of getter
// precedence. Modern full_count comes from verified DEX NumTypeIds.
inline bool Read(int api,const void* cache,size_t extent,uint32_t type_count,View* output) {
  if(api<30 || api>37 || !cache || !output)return false;
  const auto address=reinterpret_cast<uintptr_t>(cache);
  const size_t size=api<=33?sizeof(Legacy):sizeof(Dual);
  if(address%8 || extent<size || address>UINTPTR_MAX-size)return false;
  const auto* bytes=static_cast<const unsigned char*>(cache);
  auto pointer=[&](size_t offset) {return __atomic_load_n(reinterpret_cast<const uint64_t*>(bytes+offset),__ATOMIC_ACQUIRE);};
  View value;
  if(api<=33) {
    value.pairs=reinterpret_cast<Pair*>(static_cast<uintptr_t>(pointer(offsetof(Legacy,types))));
    const uint32_t count=__atomic_load_n(reinterpret_cast<const uint32_t*>(bytes+offsetof(Legacy,num_types)),__ATOMIC_RELAXED);
    if(count>abi::arm64::android11::kTypeCacheSlots)return false;
    value.pair_count=value.pairs?count:0;
  } else {
    value.pairs=reinterpret_cast<Pair*>(static_cast<uintptr_t>(pointer(offsetof(Dual,types))));
    value.pair_count=value.pairs?abi::arm64::android11::kTypeCacheSlots:0;
    value.full=reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(pointer(offsetof(Dual,types_array))));
    value.full_count=value.full?type_count:0;
  }
  const auto pair_address=reinterpret_cast<uintptr_t>(value.pairs),full_address=reinterpret_cast<uintptr_t>(value.full);
  if((value.pairs && (pair_address%8 || value.pair_count>(UINTPTR_MAX-pair_address)/sizeof(Pair))) ||
     (value.full && (full_address%4 || value.full_count>(UINTPTR_MAX-full_address)/4)))return false;
  *output=value;return true;
}
}
