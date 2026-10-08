#include "platform/class_metadata_abi.h"
#include <array>
#include <cassert>
#include <iostream>
#include <thread>
namespace g=artdcevm::class_metadata;
int main() {
  alignas(8) std::array<unsigned char,120> bytes{};
  auto put=[&](size_t offset,uint32_t value){std::memcpy(bytes.data()+offset,&value,4);};
  put(16,0x12345678);put(64,0x11112222);put(56,0x5555aaaa);
  put(80,65538);put(84,9);put(72,131076);put(76,13);
  for(int api=30;api<=37;++api) {
    g::Identity value{};
    assert(g::ReadIdentity(api,bytes.data(),bytes.size(),&value));
    assert(value.dex_cache==0x12345678);
    assert(value.access_flags==(api<=35?0x11112222:0x5555aaaa));
    assert(value.class_def_index==(api<=35?65538:131076) && value.dex_type_index==(api<=35?9:13));
    assert(g::Declaration(api)->size==(api<=35?120:api==36?112:108));
    g::Identity sentinel{1,2,3,4};
    assert(!g::ReadIdentity(api,bytes.data(),g::Declaration(api)->size-1,&sentinel));
    assert(sentinel.dex_cache==1 && sentinel.access_flags==2 && sentinel.class_def_index==3 && sentinel.dex_type_index==4);
  }
  g::Identity sentinel{1,2,3,4};
  for(int api:{29,38})assert(!g::ReadIdentity(api,bytes.data(),bytes.size(),&sentinel));
  assert(!g::ReadIdentity(33,bytes.data()+1,120,&sentinel));
  assert(!g::ReadIdentity(33,nullptr,120,&sentinel));
  assert(!g::ReadIdentity(33,reinterpret_cast<const void*>(UINTPTR_MAX-3),120,&sentinel));
  assert(!g::ReadIdentity(33,bytes.data(),120,nullptr));
  assert(sentinel.class_def_index==3 && sentinel.access_flags==2);
  put(72,UINT32_MAX);g::Identity no_definition{};
  assert(g::ReadIdentity(37,bytes.data(),120,&no_definition) && no_definition.class_def_index==-1);
  auto* flags=reinterpret_cast<uint32_t*>(bytes.data()+56);
  std::thread writer([&]{for(unsigned i=0;i<200000;++i){
    __atomic_store_n(flags,0x5555aaaa,__ATOMIC_RELAXED);
    __atomic_store_n(flags,0xaaaa5555,__ATOMIC_RELAXED);
  }});
  for(unsigned i=0;i<200000;++i) {
    g::Identity value{};assert(g::ReadIdentity(36,bytes.data(),120,&value));
    assert(value.access_flags==0x5555aaaa || value.access_flags==0xaaaa5555);
  }
  writer.join();
  std::cout<<"Class metadata: API30-37 source families, full signed DEX indices, bounded opaque references and atomic flags passed\n";
}
