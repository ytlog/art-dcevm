#include "platform/android13/dex_cache_method_abi.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
namespace g=artdcevm::android13::dex_cache_method_abi;
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
  std::cout<<"DexCache method pairs: cache misses, index collisions, bounds and concurrent pair identity passed\n";
}
