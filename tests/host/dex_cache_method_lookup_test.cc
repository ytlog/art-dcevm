#include "platform/android13/dex_cache_method_abi.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
namespace g=artdcevm::android13::dex_cache_method_abi;
namespace modern=artdcevm::method_cache;
int main() {
  int first,second;void* result=&first;
  assert(g::Lookup(nullptr,0,0,&result) && !result);
  assert(g::Lookup(nullptr,1,0,&result) && !result);
  assert(!g::Lookup(nullptr,g::kSlots+1,0,&result));
  auto table=std::make_unique<std::atomic<g::Pair>[]>(g::kSlots);
  for(unsigned i=0;i<g::kSlots;++i)table[i].store({0,0},std::memory_order_relaxed);
  constexpr uint32_t index=17,collision=index+g::kSlots;
  table[index].store({reinterpret_cast<uintptr_t>(&first),index},std::memory_order_release);
  assert(g::Lookup(table.get(),g::kSlots,index,&result) && result==&first);
  assert(g::Lookup(table.get(),g::kSlots,collision,&result) && !result);
  assert(!g::Lookup(table.get(),index,index,&result));
  assert(!g::Lookup(table.get(),g::kSlots+1,index,&result));
  assert(!g::Lookup(table.get(),g::kSlots,index,nullptr));
  auto* unaligned=reinterpret_cast<const std::atomic<g::Pair>*>(reinterpret_cast<char*>(table.get())+8);
  assert(!g::Lookup(unaligned,g::kSlots,index,&result));
  // Full arrays contain direct method pointers, not hashed pointer/index pairs.
  // They take precedence over pairs, including when the full entry is empty.
  constexpr uint32_t method_count = collision + 1;
  auto full = std::make_unique<std::atomic<uintptr_t>[]>(method_count);
  for (unsigned i = 0; i < method_count; ++i) full[i].store(0, std::memory_order_relaxed);
  full[collision].store(reinterpret_cast<uintptr_t>(&second), std::memory_order_relaxed);
  assert(modern::LookupDual(34,full.get(), table.get(), method_count, collision, &result) && result == &second);
  assert(modern::LookupDual(34,full.get(), table.get(), method_count, index, &result) && !result);
  assert(modern::LookupDual(34,nullptr, table.get(), method_count, index, &result) && result == &first);
  assert(modern::LookupDual(34,nullptr, table.get(), method_count, collision, &result) && !result);
  assert(modern::LookupDual(34,nullptr, nullptr, method_count, index, &result) && !result);
  assert(!modern::LookupDual(34,full.get(), table.get(), method_count, method_count, &result) && !result);
  assert(!modern::LookupDual(34,nullptr, table.get(), 0, 0, &result) && !result);
  assert(!modern::LookupDual(34,full.get(), table.get(), method_count, index, nullptr));
  const auto* bad_full = reinterpret_cast<const std::atomic<uintptr_t>*>(reinterpret_cast<const char*>(full.get()) + 1);
  assert(!modern::LookupDual(34,bad_full, table.get(), method_count, index, &result) && !result);
  // The unused pair pointer is not consulted when a full array is present.
  assert(modern::LookupDual(34,full.get(), unaligned, method_count, index, &result) && !result);
  assert(!modern::LookupDual(34,nullptr, unaligned, method_count, index, &result) && !result);
  std::thread full_writer([&] {
    for (unsigned i = 0; i < 200000; ++i) {
      full[index].store(reinterpret_cast<uintptr_t>(&first), std::memory_order_relaxed);
      full[index].store(0, std::memory_order_relaxed);
    }
  });
  for (unsigned i = 0; i < 200000; ++i)
    assert(modern::LookupDual(34,full.get(), table.get(), method_count, index, &result) && (!result || result == &first));
  full_writer.join();
  auto sequence = std::make_unique<modern::SequencePair[]>(g::kSlots);
  for(unsigned i=0;i<g::kSlots;++i) {
    sequence[i].key.store(i?0:1,std::memory_order_relaxed);
    sequence[i].method.store(0,std::memory_order_relaxed);
  }
  auto store_sequence = [&](uint32_t id, uintptr_t method) {
    auto& pair=sequence[id%g::kSlots];
    uint64_t key=pair.key.load(std::memory_order_relaxed);
    do { key &= ~modern::kSequenceLock; }
    while(!pair.key.compare_exchange_weak(key,key|modern::kSequenceLock));
    const uint64_t next=(((key&modern::kSequenceMask)+0x100000000ull)&~modern::kSequenceLock)|id;
    pair.method.store(method,std::memory_order_release);
    pair.key.store(next,std::memory_order_release);
  };
  store_sequence(index,reinterpret_cast<uintptr_t>(&first));
  for(int api=35;api<=37;++api) {
    assert(modern::LookupDual(api,nullptr,sequence.get(),method_count,index,&result) && result==&first);
    assert(modern::LookupDual(api,nullptr,sequence.get(),method_count,collision,&result) && !result);
    full[index].store(0,std::memory_order_relaxed);
    assert(modern::LookupDual(api,full.get(),sequence.get(),method_count,index,&result) && !result);
    assert(modern::LookupDual(api,nullptr,nullptr,method_count,index,&result) && !result);
  }
  assert(!modern::LookupDual(33,nullptr,sequence.get(),method_count,index,&result));
  assert(!modern::LookupDual(38,nullptr,sequence.get(),method_count,index,&result));
  const auto* bad_sequence=reinterpret_cast<const char*>(sequence.get())+8;
  assert(!modern::LookupDual(35,nullptr,bad_sequence,method_count,index,&result));
  sequence[index].key.fetch_or(modern::kSequenceLock,std::memory_order_release);
  std::atomic<bool> release_lock{false};
  std::thread lock_writer([&] {
    while(!release_lock.load(std::memory_order_acquire)){}
    sequence[index].key.fetch_and(~modern::kSequenceLock,std::memory_order_release);
  });
  release_lock.store(true,std::memory_order_release);
  assert(modern::LookupDual(35,nullptr,sequence.get(),method_count,index,&result) && result==&first);
  lock_writer.join();
  std::thread sequence_writer([&] {
    for(unsigned i=0;i<200000;++i) {
      store_sequence(index,reinterpret_cast<uintptr_t>(&first));
      store_sequence(collision,reinterpret_cast<uintptr_t>(&second));
    }
  });
  for(unsigned i=0;i<200000;++i) {
    assert(modern::LookupDual(35,nullptr,sequence.get(),method_count,index,&result) && (!result || result==&first));
    assert(modern::LookupDual(35,nullptr,sequence.get(),method_count,collision,&result) && (!result || result==&second));
  }
  sequence_writer.join();
  // Exercise actual source-declared managed fields, rather than only tables.
  alignas(8) modern::LegacyCache legacy{};
  legacy.methods=reinterpret_cast<uintptr_t>(table.get());legacy.num_methods=g::kSlots;
  table[index].store({reinterpret_cast<uintptr_t>(&first),index},std::memory_order_release);
  for(int api=30;api<=33;++api) {
    assert(modern::ReadCache(api,&legacy,sizeof(legacy),0,index,&result) && result==&first);
    assert(!modern::ReadCache(api,&legacy,sizeof(legacy)-1,0,index,&result) && !result);
  }
  alignas(8) modern::DualCache dual{};
  full[index].store(0,std::memory_order_relaxed);
  dual.methods_array=reinterpret_cast<uintptr_t>(full.get());
  for(int api=34;api<=37;++api) {
    dual.methods=reinterpret_cast<uintptr_t>(bad_sequence);
    assert(modern::ReadCache(api,&dual,sizeof(dual),method_count,index,&result) && !result);
    assert(!modern::ReadCache(api,&dual,sizeof(dual)-1,method_count,index,&result) && !result);
    dual.methods_array=0;
    dual.methods=reinterpret_cast<uintptr_t>(api==34?static_cast<const void*>(table.get()):sequence.get());
    store_sequence(index,reinterpret_cast<uintptr_t>(&first));
    assert(modern::ReadCache(api,&dual,sizeof(dual),method_count,index,&result) && result==&first);
    dual.methods_array=reinterpret_cast<uintptr_t>(full.get());
  }
  assert(!modern::ReadCache(29,&legacy,sizeof(legacy),0,index,&result));
  assert(!modern::ReadCache(38,&dual,sizeof(dual),method_count,index,&result));
  assert(!modern::ReadCache(33,nullptr,sizeof(legacy),0,index,&result));
  assert(!modern::ReadCache(33,reinterpret_cast<const char*>(&legacy)+4,sizeof(legacy),0,index,&result));
  assert(!modern::ReadCache(33,&legacy,sizeof(legacy),0,index,nullptr));
  assert(!modern::ReadCache(33,reinterpret_cast<const void*>(UINTPTR_MAX-7),sizeof(legacy),0,index,&result));
  // AllocArray publishes the count before its array pointer. Acquiring the
  // pointer must make the table and matching count visible to cache readers.
  alignas(8) modern::LegacyCache published{};
  auto* published_pointer=reinterpret_cast<uint64_t*>(reinterpret_cast<unsigned char*>(&published)+offsetof(modern::LegacyCache,methods));
  std::thread publisher([&] {
    __atomic_store_n(&published.num_methods,g::kSlots,__ATOMIC_RELAXED);
    __atomic_store_n(published_pointer,reinterpret_cast<uintptr_t>(table.get()),__ATOMIC_RELEASE);
  });
  while(!__atomic_load_n(published_pointer,__ATOMIC_ACQUIRE)){}
  assert(modern::ReadCache(33,&published,sizeof(published),0,index,&result) && result==&first);
  publisher.join();
  for(int api=34;api<=37;++api) {
    alignas(8) modern::DualCache modern_publication{};
    auto* array_pointer=reinterpret_cast<uint64_t*>(reinterpret_cast<unsigned char*>(&modern_publication)+offsetof(modern::DualCache,methods_array));
    full[index].store(0,std::memory_order_relaxed);
    std::thread publish_full([&] {
      full[index].store(reinterpret_cast<uintptr_t>(&first),std::memory_order_relaxed);
      __atomic_store_n(array_pointer,reinterpret_cast<uintptr_t>(full.get()),__ATOMIC_RELEASE);
    });
    while(!__atomic_load_n(array_pointer,__ATOMIC_ACQUIRE)){}
    assert(modern::ReadCache(api,&modern_publication,sizeof(modern_publication),method_count,index,&result) && result==&first);
    publish_full.join();
  }
  // ART changes the pointer and index together. A reader must never report
  // the pointer from a colliding index, including during concurrent updates.
  std::atomic<bool> start{false};
  std::thread writer([&] {
    while(!start.load(std::memory_order_acquire)){}
    for(unsigned i=0;i<200000;++i) {
      table[index].store({reinterpret_cast<uintptr_t>(&first),index},std::memory_order_release);
      table[index].store({reinterpret_cast<uintptr_t>(&second),collision},std::memory_order_release);
    }
  });
  start.store(true,std::memory_order_release);
  for(unsigned i=0;i<200000;++i) {
    assert(g::Lookup(table.get(),g::kSlots,index,&result) && (!result || result==&first));
    assert(g::Lookup(table.get(),g::kSlots,collision,&result) && (!result || result==&second));
  }
  writer.join();
  std::cout<<"DexCache method pairs: cache misses, index collisions, bounds, full-array precedence/empty entries and API35-37 sequence locks and concurrent pair/full/sequence identity, API30-37 field reads and table publication passed\n";
}
