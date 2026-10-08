#include "platform/common/native_cache_reset.h"
#include <cassert>
#include <memory>
#include <iostream>
namespace g=artdcevm::native_cache;
namespace m=artdcevm::method_cache;
int main() {
  int object;void* value=&object;
  auto pairs=std::make_unique<std::atomic<m::Pair>[]>(m::kSlots);
  for(int api=30;api<=34;++api) {
    for(uint32_t i=0;i<m::kSlots;++i)pairs[i].store({reinterpret_cast<uintptr_t>(&object),i},std::memory_order_release);
    assert(g::ResetPairs(api,pairs.get(),m::kSlots));
    for(uint32_t i=0;i<m::kSlots;++i) {
      const auto pair=pairs[i].load(std::memory_order_acquire);
      assert(!pair.method && pair.index==(i==0?1u:0u));
      assert(m::Lookup(pairs.get(),m::kSlots,i,&value) && !value);
    }
  }
  auto sequence=std::make_unique<m::SequencePair[]>(m::kSlots);
  for(int api=35;api<=37;++api) {
    for(uint32_t i=0;i<m::kSlots;++i) {
      sequence[i].key.store((uint64_t{0x7fffffff}<<32)|i,std::memory_order_relaxed);
      sequence[i].method.store(reinterpret_cast<uintptr_t>(&object),std::memory_order_relaxed);
    }
    assert(g::ResetPairs(api,sequence.get(),m::kSlots));
    for(uint32_t i=0;i<m::kSlots;++i) {
      assert(sequence[i].key.load()==(i==0?1u:0u)); // Counter wraps without retaining lock.
      assert(m::LookupDual(api,nullptr,sequence.get(),m::kSlots,i,&value) && !value);
    }
  }
  auto full=std::make_unique<std::atomic<uintptr_t>[]>(2049);
  for(int api=34;api<=37;++api) {
    for(unsigned i=0;i<2049;++i)full[i].store(reinterpret_cast<uintptr_t>(&object));
    assert(g::ResetArray(api,full.get(),2049));
    for(unsigned i=0;i<2049;++i)assert(!full[i].load());
  }
  // Independent publishers race per-slot resets; readers must retain identity
  // across collisions and the reset sentinel, never mix an index and pointer.
  int other;
  constexpr uint32_t index=17,collision=index+m::kSlots;
  auto publish=[&](uint32_t id,uintptr_t object) {
    auto& slot=sequence[index];
    uint64_t key=slot.key.load();
    for(;;) {
      key&=~m::kSequenceLock;
      if(slot.key.compare_exchange_strong(key,key|m::kSequenceLock))break;
    }
    slot.method.store(object,std::memory_order_release);
    slot.key.store((((key&m::kSequenceMask)+(uint64_t{1}<<32))&~m::kSequenceLock)|id,std::memory_order_release);
  };
  std::thread writer([&]{for(unsigned i=0;i<20000;++i) {
    publish(index,reinterpret_cast<uintptr_t>(&object));publish(collision,reinterpret_cast<uintptr_t>(&other));
  }});
  std::thread resetter([&]{for(unsigned i=0;i<20000;++i)assert(g::ResetPairs(35,sequence.get(),index+1));});
  for(unsigned i=0;i<200000;++i) {
    assert(m::LookupDual(35,nullptr,sequence.get(),collision+1,index,&value) && (!value || value==&object));
    assert(m::LookupDual(35,nullptr,sequence.get(),collision+1,collision,&value) && (!value || value==&other));
  }
  writer.join();resetter.join();
  std::thread old_writer([&]{for(unsigned i=0;i<20000;++i) {
    pairs[index].store({reinterpret_cast<uintptr_t>(&object),index},std::memory_order_release);
    pairs[index].store({reinterpret_cast<uintptr_t>(&other),collision},std::memory_order_release);
  }});
  std::thread old_resetter([&]{for(unsigned i=0;i<20000;++i)assert(g::ResetPairs(33,pairs.get(),index+1));});
  for(unsigned i=0;i<200000;++i) {
    assert(m::Lookup(pairs.get(),m::kSlots,index,&value) && (!value || value==&object));
    assert(m::Lookup(pairs.get(),m::kSlots,collision,&value) && (!value || value==&other));
  }
  old_writer.join();old_resetter.join();
  const auto overflow=UINTPTR_MAX&~uintptr_t{15};
  for(int api=30;api<=37;++api) {
    assert(!g::ResetPairs(api,reinterpret_cast<void*>(overflow),1024));
    assert(!g::ResetPairs(api,reinterpret_cast<char*>(pairs.get())+8,1024));
    assert(!g::ResetPairs(api,pairs.get(),1025));
    assert(g::ResetPairs(api,nullptr,1024));
    assert(g::ResetPairs(api,pairs.get(),0));
  }
  for(int api:{29,38})assert(!g::ResetPairs(api,pairs.get(),1024));
  for(int api:{33,38})assert(!g::ResetArray(api,full.get(),2049));
  assert(!g::ResetArray(37,reinterpret_cast<std::atomic<uintptr_t>*>(overflow),2049));
  assert(!g::ResetArray(37,reinterpret_cast<std::atomic<uintptr_t>*>(reinterpret_cast<char*>(full.get())+1),2049));
  // Validation failure leaves every real slot unchanged.
  full[0].store(reinterpret_cast<uintptr_t>(&object));
  assert(!g::ResetArray(29,full.get(),2049) && full[0].load()==reinterpret_cast<uintptr_t>(&object));
  pairs[0].store({reinterpret_cast<uintptr_t>(&object),17});
  assert(!g::ResetPairs(33,pairs.get(),1025) && pairs[0].load().index==17);
  std::cout<<"Native cache reset: API30-37 pair protocols, invalid-slot sentinel, sequence wrap, full arrays, concurrent publisher/reset identity and bounds passed\n";
}
