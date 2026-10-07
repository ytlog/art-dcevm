#include "platform/method_cache_lookup.h"
#include <limits>
#include <chrono>
#include <thread>
namespace artdcevm::method_cache {
bool Lookup(const std::atomic<Pair>* table,uint32_t count,uint32_t index,void** output) {
  if(!output)return false;
  *output=nullptr;
  if(!table)return count<=kSlots;
  if(reinterpret_cast<uintptr_t>(table)%alignof(Pair) || count>kSlots || index%kSlots>=count)return false;
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
  if(reinterpret_cast<uintptr_t>(pairs)%alignof(SequencePair))return false;
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
}
