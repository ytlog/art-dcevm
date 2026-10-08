#include "platform/common/method_cache_lookup.h"
#include <limits>
#include <chrono>
#include <thread>
namespace artdcevm::method_cache {
bool Lookup(const std::atomic<Pair>* table,uint32_t count,uint32_t index,void** output) {
  if(!output)return false;
  *output=nullptr;
  if(!table)return count<=kSlots;
  const uintptr_t address=reinterpret_cast<uintptr_t>(table);
  if(address%alignof(Pair) || count>kSlots || index%kSlots>=count ||
      count>(std::numeric_limits<uintptr_t>::max()-address)/sizeof(Pair))return false;
  const Pair pair=table[index%kSlots].load(std::memory_order_acquire);
  if(pair.index==index)*output=reinterpret_cast<void*>(pair.method);
  return true;
}
bool LookupDual(int api,const std::atomic<uintptr_t>* full,const void* pairs,
                uint32_t method_count,uint32_t index,void** output) {
  if(!output)return false;
  *output=nullptr;
  if(api<34 || api>37 || index>=method_count)return false;
  if(full) {
    const uintptr_t address=reinterpret_cast<uintptr_t>(full);
    if(address%alignof(std::atomic<uintptr_t>) ||
        method_count>(std::numeric_limits<uintptr_t>::max()-address)/sizeof(uintptr_t))return false;
    *output=reinterpret_cast<void*>(full[index].load(std::memory_order_relaxed));
    return true;
  }
  if(api==34)return Lookup(static_cast<const std::atomic<Pair>*>(pairs),kSlots,index,output);
  if(!pairs)return true;
  const uintptr_t address=reinterpret_cast<uintptr_t>(pairs);
  if(address%alignof(SequencePair) ||
      kSlots>(std::numeric_limits<uintptr_t>::max()-address)/sizeof(SequencePair))return false;
  const auto& pair=static_cast<const SequencePair*>(pairs)[index%kSlots];
  for(uint32_t spins=0;;++spins) {
    const uint64_t before=pair.key.load(std::memory_order_acquire);
    const uint64_t method=pair.method.load(std::memory_order_acquire);
    const uint64_t after=pair.key.load(std::memory_order_relaxed);
    if(!(before&kSequenceLock) && before==after) {
      if((before&~kSequenceMask)==index)*output=reinterpret_cast<void*>(method);
      return true;
    }
    // Match ART's contention backoff without changing its mutator-lock state.
    if(spins>10000)std::this_thread::sleep_for(std::chrono::nanoseconds(100000));
  }
}
bool ReadCache(int api,const void* cache,size_t extent,uint32_t method_count,
               uint32_t index,void** output) {
  if(!output)return false;
  *output=nullptr;
  if(api<30 || api>37 || !cache || reinterpret_cast<uintptr_t>(cache)%alignof(uint64_t))return false;
  const size_t size=api<=33?sizeof(LegacyCache):sizeof(DualCache);
  if(extent<size || reinterpret_cast<uintptr_t>(cache)>std::numeric_limits<uintptr_t>::max()-size)return false;
  const auto* bytes=static_cast<const unsigned char*>(cache);
  auto pointer=[&](size_t offset) {
    return __atomic_load_n(reinterpret_cast<const uint64_t*>(bytes+offset),__ATOMIC_ACQUIRE);
  };
  if(api<=33) {
    const auto address=pointer(offsetof(LegacyCache,methods));
    const uint32_t count=__atomic_load_n(reinterpret_cast<const uint32_t*>(bytes+offsetof(LegacyCache,num_methods)),__ATOMIC_RELAXED);
    return Lookup(reinterpret_cast<const std::atomic<Pair>*>(address),count,index,output);
  }
  const auto full=pointer(offsetof(DualCache,methods_array));
  // The unused pointer is not read after choosing the full-array branch.
  const auto pairs=full?0:pointer(offsetof(DualCache,methods));
  return LookupDual(api,reinterpret_cast<const std::atomic<uintptr_t>*>(full),
                    reinterpret_cast<const void*>(pairs),method_count,index,output);
}

}
