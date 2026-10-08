#include "platform/field_array_view.h"
#include <array>
#include <cassert>
#include <iostream>
namespace g=artdcevm::field_array;
int main() {
  alignas(8) std::array<unsigned char,120> klass{};
  alignas(4) std::array<unsigned char,68> array{};
  auto pointer=[&](size_t offset,uint64_t n){std::memcpy(klass.data()+offset,&n,8);};
  auto put=[&](size_t offset,uint32_t n){std::memcpy(array.data()+offset,&n,4);};
  pointer(40,reinterpret_cast<uintptr_t>(array.data()));pointer(56,0x12345678);
  put(0,4);
  // Merged fields are ordered by DEX field ID, with alternating staticness.
  for(size_t i=0;i<4;++i){put(4+16*i+4,i%2?8:1);put(4+16*i+8,100+i);put(4+16*i+12,200+4*i);}
  for(int api=30;api<=37;++api) {
    void* selected=nullptr;
    assert(g::ReadPointer(api,klass.data(),klass.size(),false,&selected) && selected==array.data());
    assert(g::ReadPointer(api,klass.data(),klass.size(),true,&selected));
    assert(selected==(api>=36?static_cast<void*>(array.data()):reinterpret_cast<void*>(0x12345678)));
    g::View trusted;
    assert(g::ReadTrustedClass(api,klass.data(),klass.size(),false,&trusted));
    assert(trusted.count==4 && trusted.selected_count==(api>=36?2u:4u));
    for(bool statik:{false,true}) {
      g::View view;
      assert(g::Read(api,array.data(),array.size(),statik,&view));
      assert(view.count==4 && view.selected_count==(api>=36?2u:4u));
      assert(view.merged==(api>=36) && view.is_static==statik);
      assert(view.At(3)==array.data()+52 && !view.At(4));
      for(uint32_t i=0;i<view.selected_count;++i) {
        const uint32_t physical=api>=36?2*i+statik:i;
        auto* field=view.AtSelected(i);assert(field==array.data()+4+16*physical);
        uint32_t index=99;assert(view.IndexOf(field,&index) && index==physical);
        uint32_t dex_index;std::memcpy(&dex_index,static_cast<unsigned char*>(field)+8,4);
        assert(dex_index==100+physical);
      }
      assert(!view.AtSelected(view.selected_count));
      uint32_t sentinel=99;
      assert(!view.IndexOf(array.data()+5,&sentinel));
      assert(!view.IndexOf(array.data()+68,&sentinel));
      assert(!view.IndexOf(array.data(),&sentinel));
      if(api>=36)assert(!view.IndexOf(view.At(statik?0:1),&sentinel));
      assert(sentinel==99);
    }
  }
  void* sentinel=reinterpret_cast<void*>(0xabc);
  for(int api:{29,38})assert(!g::ReadPointer(api,klass.data(),120,false,&sentinel));
  assert(!g::ReadPointer(37,klass.data(),107,false,&sentinel));
  assert(!g::ReadPointer(37,klass.data()+1,120,false,&sentinel));
  assert(!g::ReadPointer(37,nullptr,120,false,&sentinel));
  assert(!g::ReadPointer(37,reinterpret_cast<void*>(UINTPTR_MAX-3),120,false,&sentinel));
  assert(!g::ReadPointer(37,klass.data(),120,false,nullptr));
  assert(sentinel==reinterpret_cast<void*>(0xabc));
  g::View unchanged{nullptr,11,22,false,false};
  for(size_t n=0;n<array.size();++n)assert(!g::Read(37,array.data(),n,true,&unchanged));
  assert(!g::Read(37,array.data()+1,array.size(),true,&unchanged));
  assert(!g::Read(37,reinterpret_cast<void*>(UINTPTR_MAX-3),4,true,&unchanged));
  assert(!g::Read(37,nullptr,4,true,&unchanged));
  assert(!g::Read(29,array.data(),array.size(),true,&unchanged));
  assert(!g::Read(37,array.data(),array.size(),true,nullptr));
  assert(unchanged.count==11 && unchanged.selected_count==22 && !unchanged.merged);
  put(0,UINT32_MAX);assert(!g::Read(37,array.data(),array.size(),true,&unchanged));
  assert(!g::ReadTrustedClass(37,klass.data(),108,false,nullptr));
  g::View empty;
  assert(g::Read(37,nullptr,0,true,&empty) && !empty.At(0) && !empty.AtSelected(0));
  put(0,0);assert(g::Read(37,array.data(),4,false,&empty) && !empty.count);
  std::cout<<"Field arrays: API30-37 separated/merged pointers, interleaved static filters, physical indices, layout/bounds and failure output passed\n";
}
