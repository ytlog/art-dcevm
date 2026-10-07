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
  std::cout<<"DexCache method pairs: cache misses, index collisions, bounds, full-array precedence/empty entries and API35-37 sequence locks and concurrent pair/full/sequence identity passed\n";
}
