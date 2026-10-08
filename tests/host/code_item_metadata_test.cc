#include "platform/common/code_item_metadata.h"
#include <array>
#include <cassert>
#include <iostream>
namespace c=artdcevm::code_item_metadata;
namespace m=artdcevm::method_metadata;
int main() {
 alignas(8) std::array<unsigned char,256> bytes{};
 auto put=[&](size_t at,uint16_t value){std::memcpy(bytes.data()+at,&value,2);};
 artdcevm::evolution::MappedDexMetadata map{bytes.data(),bytes.size(),bytes.data()+64,192,0};
 alignas(8) m::Method11 old{};alignas(8) m::Method12 modern{};
 old.dex_method_index=modern.dex_method_index=1;old.dex_code_item_offset=64;
 modern.data=reinterpret_cast<uintptr_t>(bytes.data()+128);
 for(int api=30;api<=37;++api) {
  void* method=api==30?static_cast<void*>(&old):static_cast<void*>(&modern);
  size_t extent=api==30?40:32;
  std::memcpy(bytes.data(),"dex\n035",8);put(128,23);
  uint32_t count=99;assert(c::ReadRegisters(api,method,extent,map,&count) && count==23);
  count=99;assert(!c::ReadRegisters(api,method,extent-1,map,&count) && count==99);
  assert(!c::ReadRegisters(api,method,extent,map,nullptr));
  // Modern pointers determine format even when owning file magic is CDEX.
  std::memcpy(bytes.data(),"cdex001",8);
  if(api!=30)assert(c::ReadRegisters(api,method,extent,map,&count) && count==23);
  if(api<=35) {
   modern.data|=1;put(128,0x2100);put(130,19);put(126,0);put(124,0);put(122,32);put(120,16);
   assert(c::ReadRegisters(api,method,extent,map,&count) && count==51);
   map.data_begin=bytes.data()+128;map.data_size=128;map.begin=bytes.data()+128;map.size=128;
   count=99;assert(!c::ReadRegisters(api,method,extent,map,&count) && count==99);
   map={bytes.data(),bytes.size(),bytes.data()+64,192,0};
  } else {
   modern.data|=1;count=99;assert(!c::ReadRegisters(api,method,extent,map,&count) && count==99);
  }
  modern.data=reinterpret_cast<uintptr_t>(bytes.data()+128);
 }
 uint32_t sentinel=77;
 for(int api:{29,38})assert(!c::ReadRegisters(api,&modern,32,map,&sentinel));
 modern.access_flags=0x100;assert(!c::ReadRegisters(33,&modern,32,map,&sentinel));modern.access_flags=0x400;
 assert(!c::ReadRegisters(33,&modern,32,map,&sentinel));modern.access_flags=0;modern.dex_method_index=UINT32_MAX;
 assert(!c::ReadRegisters(33,&modern,32,map,&sentinel));modern.dex_method_index=1;
 modern.data=reinterpret_cast<uintptr_t>(bytes.data()+254);assert(!c::ReadRegisters(33,&modern,32,map,&sentinel));
 modern.data=0;assert(!c::ReadRegisters(33,&modern,32,map,&sentinel));
 old.dex_code_item_offset=0;assert(!c::ReadRegisters(30,&old,40,map,&sentinel));
 old.dex_code_item_offset=UINT32_MAX;assert(!c::ReadRegisters(30,&old,40,map,&sentinel));
 assert(sentinel==77);
 std::cout<<"Code item registers: API30-37 offset/tag/untagged storage, standard in CDEX owner, backward extensions, bounds and failure output passed\n";
}
