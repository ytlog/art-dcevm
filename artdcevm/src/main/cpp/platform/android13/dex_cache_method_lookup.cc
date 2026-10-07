#include "platform/android13/dex_cache_method_abi.h"
namespace artdcevm::android13 {
namespace dex_cache_method_abi {
bool Lookup(const std::atomic<Pair>* table,uint32_t count,uint32_t index,void** output) {
  if(!output)return false;
  *output=nullptr;
  if(!table)return count<=kSlots;
  if(reinterpret_cast<uintptr_t>(table)%alignof(Pair) || count>kSlots || index%kSlots>=count)return false;
  const Pair pair=table[index%kSlots].load(std::memory_order_acquire);
  if(pair.index==index)*output=reinterpret_cast<void*>(pair.method);
  return true;
}
}
}
