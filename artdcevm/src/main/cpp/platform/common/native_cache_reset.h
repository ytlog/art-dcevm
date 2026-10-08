#pragma once
#include "platform/common/method_cache_lookup.h"
#include <chrono>
#include <limits>
#include <thread>
namespace artdcevm::native_cache {
// Native ArtField/ArtMethod caches only; this never writes managed GcRoot
// pairs. Caller owns calibrated live storage and keeps its publication and
// lifetime stable under the proper ART lock. Reset is per-slot, not an atomic
// cache-wide transaction. A full array and a pair array may both need reset.
inline bool ResetPairs(int api,void* table,uint32_t count) {
  using namespace method_cache;
  if(api<30 || api>37 || count>kSlots)return false;
  if(!table)return true;
  const auto address=reinterpret_cast<uintptr_t>(table);
  if(address%16 || count>(UINTPTR_MAX-address)/16)return false;
  for(uint32_t i=0;i<count;++i) {
    const uint64_t invalid=i==0?1:0; // Source InvalidIndexForSlot.
    if(api<=34) {
      Pair empty{0,static_cast<uintptr_t>(invalid)};
      __atomic_store(static_cast<Pair*>(table)+i,&empty,__ATOMIC_RELEASE);
    } else {
      auto& slot=static_cast<SequencePair*>(table)[i];
      uint64_t key=slot.key.load(std::memory_order_relaxed);
      for(uint32_t spins=0;;++spins) {
        key&=~kSequenceLock;
        if(slot.key.compare_exchange_weak(key,key|kSequenceLock))break;
        if(spins>10000)std::this_thread::sleep_for(std::chrono::nanoseconds(100000));
      }
      key=(((key&kSequenceMask)+(uint64_t{1}<<32))&~kSequenceLock)|invalid;
      slot.method.store(0,std::memory_order_release);
      slot.key.store(key,std::memory_order_release);
    }
  }
  return true;
}
// Full NativeArray storage contains native pointers only; count is the
// verified DEX NumFieldIds/NumMethodIds, not the fixed pair-slot count.
inline bool ResetArray(int api,std::atomic<uintptr_t>* table,uint32_t count) {
  if(api<34 || api>37)return false;
  if(!table)return true;
  const auto address=reinterpret_cast<uintptr_t>(table);
  if(address%alignof(std::atomic<uintptr_t>) || count>(UINTPTR_MAX-address)/sizeof(uintptr_t))return false;
  for(uint32_t i=0;i<count;++i)table[i].store(0,std::memory_order_relaxed);
  return true;
}
}
