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
 for(const auto& layout:{c::kOffset,c::kTagged,c::kStandard}) {
  void* method=layout.storage==c::Storage::DexOffset?static_cast<void*>(&old):static_cast<void*>(&modern);
  size_t extent=layout.storage==c::Storage::DexOffset?40:32;
  std::memcpy(bytes.data(),"dex\n035",8);put(128,23);
  uint32_t count=99;assert(c::ReadRegisters(layout,method,extent,map,&count) && count==23);
  count=99;assert(!c::ReadRegisters(layout,method,extent-1,map,&count) && count==99);
  assert(!c::ReadRegisters(layout,method,extent,map,nullptr));
  // Modern pointers determine format even when owning file magic is CDEX.
  std::memcpy(bytes.data(),"cdex001",8);
  if(layout.storage!=c::Storage::DexOffset)assert(c::ReadRegisters(layout,method,extent,map,&count) && count==23);
  if(layout.storage!=c::Storage::StandardPointer) {
   modern.data|=1;put(128,0x2100);put(130,19);put(126,0);put(124,0);put(122,32);put(120,16);
   assert(c::ReadRegisters(layout,method,extent,map,&count) && count==51);
   map.data_begin=bytes.data()+128;map.data_size=128;map.begin=bytes.data()+128;map.size=128;
   count=99;assert(!c::ReadRegisters(layout,method,extent,map,&count) && count==99);
   map={bytes.data(),bytes.size(),bytes.data()+64,192,0};
  } else {
   modern.data|=1;count=99;assert(!c::ReadRegisters(layout,method,extent,map,&count) && count==99);
  }
  modern.data=reinterpret_cast<uintptr_t>(bytes.data()+128);
 }
 uint32_t sentinel=77;
 // Complete Method11/12 source extents and tagged/untagged semantics differ.
 assert(!c::ReadRegisters(c::kOffset,&modern,sizeof(modern),map,&sentinel));
 modern.data=reinterpret_cast<uintptr_t>(bytes.data()+128)|1;
 assert(!c::ReadRegisters(c::kStandard,&modern,sizeof(modern),map,&sentinel));
 modern.data=reinterpret_cast<uintptr_t>(bytes.data()+128);
 auto bad=c::kTagged;bad.storage=static_cast<c::Storage>(-1);
 assert(!c::ReadRegisters(bad,&modern,32,map,&sentinel));
 bad=c::kTagged;bad.method.data=bad.method.size;
 assert(!c::ReadRegisters(bad,&modern,32,map,&sentinel));
 modern.access_flags=0x100;assert(!c::ReadRegisters(c::kTagged,&modern,32,map,&sentinel));modern.access_flags=0x400;
 assert(!c::ReadRegisters(c::kTagged,&modern,32,map,&sentinel));modern.access_flags=0;modern.dex_method_index=UINT32_MAX;
 assert(!c::ReadRegisters(c::kTagged,&modern,32,map,&sentinel));modern.dex_method_index=1;
 modern.data=reinterpret_cast<uintptr_t>(bytes.data()+254);assert(!c::ReadRegisters(c::kTagged,&modern,32,map,&sentinel));
 modern.data=0;assert(!c::ReadRegisters(c::kTagged,&modern,32,map,&sentinel));
 old.dex_code_item_offset=0;assert(!c::ReadRegisters(c::kOffset,&old,40,map,&sentinel));
 old.dex_code_item_offset=UINT32_MAX;assert(!c::ReadRegisters(c::kOffset,&old,40,map,&sentinel));
 assert(sentinel==77);
 std::cout<<"Code item registers: fixed source offset/tag/untagged storage, standard in CDEX owner, backward extensions, bounds and failure output passed\n";
}
