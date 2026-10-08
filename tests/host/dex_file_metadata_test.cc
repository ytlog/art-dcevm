#include "platform/common/dex_file_metadata.h"
#include <array>
#include <cassert>
#include <iostream>
namespace g=artdcevm::dex_file_metadata;
int main() {
  alignas(8) std::array<unsigned char,320> data{};
  alignas(8) std::array<unsigned char,24> cache{};
  auto* header=data.data()+64;
  std::memcpy(header,"dex\n040\0",8);
  const uint32_t file_size=256;std::memcpy(header+32,&file_size,4);
  artdcevm::dex_mapping_abi::Prefix prefix{nullptr,header,256,data.data(),data.size()};
  const uint64_t native=reinterpret_cast<uintptr_t>(&prefix);std::memcpy(cache.data()+16,&native,8);
  for(int api=30;api<=37;++api) {
    prefix.size=api>=35?SIZE_MAX:256;
    artdcevm::evolution::MappedDexMetadata mapped{};
    std::string error="old";
    assert(g::ReadCache(api,cache.data(),cache.size(),sizeof(prefix),131072,&mapped,&error));
    assert(mapped.begin==header && mapped.size==256 && mapped.data_begin==data.data() &&
        mapped.data_size==320 && mapped.class_def_index==131072 && error.empty());
    auto sentinel=mapped;
    assert(!g::ReadCache(api,cache.data(),23,sizeof(prefix),0,&sentinel,&error));
    assert(!g::ReadCache(api,cache.data(),24,sizeof(prefix)-1,0,&sentinel,&error));
    assert(!g::ReadCache(api,cache.data()+1,24,sizeof(prefix),0,&sentinel,&error));
    assert(sentinel.class_def_index==131072 && sentinel.begin==header && sentinel.size==256 && !error.empty());
    assert(!g::ReadFile(api,reinterpret_cast<void*>(UINTPTR_MAX-7),40,0,&sentinel,&error));
    assert(!g::ReadCache(api,reinterpret_cast<void*>(UINTPTR_MAX-3),24,40,0,&sentinel,&error));
    assert(sentinel.class_def_index==131072);
  }
  artdcevm::evolution::MappedDexMetadata sentinel{header,7,data.data(),9,11};std::string error;
  for(int api:{29,38})assert(!g::ReadCache(api,cache.data(),24,40,0,&sentinel,&error));
  assert(!g::ReadCache(33,nullptr,24,40,0,&sentinel,&error));
  assert(!g::ReadCache(33,cache.data(),24,40,0,nullptr,&error));
  assert(!g::ReadFile(33,nullptr,40,0,&sentinel,&error));
  assert(!g::ReadFile(33,reinterpret_cast<unsigned char*>(&prefix)+1,40,0,&sentinel,&error));
  uint64_t zero=0;std::memcpy(cache.data()+16,&zero,8);
  assert(!g::ReadCache(33,cache.data(),24,40,0,&sentinel,&error));
  std::memcpy(cache.data()+16,&native,8);
  prefix.data_size=100;
  assert(!g::ReadCache(35,cache.data(),24,40,0,&sentinel,&error));
  assert(sentinel.class_def_index==11 && sentinel.size==7 && sentinel.data_size==9);
  prefix.data_size=data.size();prefix.size=SIZE_MAX;
  assert(g::ReadCache(37,cache.data(),24,40,0,&sentinel,nullptr));
  // Earlier CompactDex may have independently bounded metadata and data.
  alignas(8) std::array<unsigned char,136> compact{};std::memcpy(compact.data(),"cdex001\0",8);
  prefix.begin=compact.data();prefix.size=compact.size();
  assert(g::ReadCache(33,cache.data(),24,40,0,&sentinel,&error));
  assert(sentinel.begin==compact.data() && sentinel.size==136 && sentinel.data_begin==data.data());
  assert(!g::ReadCache(35,cache.data(),24,40,0,&sentinel,&error));
  assert(sentinel.begin==compact.data());
  std::cout<<"DexFile metadata: API30-37 immutable cache link, prefix bounds, modern ignored size/header containment, independent CompactDex and failure output passed\n";
}
