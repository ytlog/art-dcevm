#include "platform/common/decoded_frame_recovery.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace artdcevm;
using namespace decoded_frame_recovery;
struct Bits {
  std::vector<unsigned char> data;
  explicit Bits(size_t count):data((count+7)/8){}
  void Raw(size_t offset,unsigned width,uint32_t value) {
    for(unsigned b=0;b<width;++b) {
      auto mask=static_cast<unsigned char>(1u<<((offset+b)%8));
      if((value>>b)&1u)data[(offset+b)/8]|=mask;else data[(offset+b)/8]&=~mask;
    }
  }
  void Cell(size_t row,size_t stride,size_t column,uint32_t value) {Raw(row*stride+column*8,8,value+1);}
};
template<size_t N> void Bind(abi::arm64::android11::BitTable<N>& table,Bits& bits,uint32_t rows,unsigned width) {
  table.region={bits.data.data(),0,bits.data.size()*8};table.rows=rows;
  for(size_t c=0;c<=N;++c)table.columns[c]=c*width;
}
template<size_t N> void Bind(abi::arm64::android17::BitTable<N>& table,Bits& bits,uint32_t rows,unsigned width) {
  table.region={bits.data.data(),0,bits.data.size()*8};table.rows=rows;table.row_bits=N*width;
  for(size_t c=0;c<N;++c)table.columns[c]=c*width;
}
template<class T> struct Fixture {
  T object{};Bits stack{128},inline_info{48},mask{8},maps{32},catalog{64};
  Fixture() {
    object.dex_regs=2;
    Bind(object.stack_maps,stack,2,8);Bind(object.inline_infos,inline_info,1,8);
    Bind(object.dex_masks,mask,2,4);Bind(object.dex_maps,maps,4,8);Bind(object.dex_catalog,catalog,4,8);
    stack.Cell(0,64,5,0);stack.Cell(1,64,5,0);
    stack.Cell(0,64,6,0);stack.Cell(0,64,7,0);
    stack.Cell(1,64,6,1);stack.Cell(1,64,7,3);
    inline_info.Cell(0,48,0,UINT32_MAX);inline_info.Cell(0,48,5,4);
    mask.Raw(0,4,7);mask.Raw(4,4,2);
    for(unsigned i=0;i<4;++i)maps.Cell(i,8,0,i);
    catalog.Cell(0,16,0,0);catalog.Cell(0,16,1,2); // stack byte offset8
    catalog.Cell(1,16,0,2);catalog.Cell(1,16,1,9); // core register9
    catalog.Cell(2,16,0,5);catalog.Cell(2,16,1,7); // FP high7
    catalog.Cell(3,16,0,1);catalog.Cell(3,16,1,17); // most recent constant17
  }
  code_info_view::View View() {code_info_view::View view;assert(code_info_view::ReadDecoded(object,&view));return view;}
};
template<class T> void Check(code_info_view::SourceFamily family) {
  Fixture<T> fixture;auto view=fixture.View();Location values[4];RegisterRange range;
  assert(code_info_view::ReadSourceObject(family,&fixture.object,sizeof(T),&view));
  assert(view.tables[4].column_count==
      (family==code_info_view::SourceFamily::kAndroid11 ||
       family==code_info_view::SourceFamily::kAndroid12 ? 1u : 3u));
  assert(DecodeSourceFrame(family,&fixture.object,sizeof(T),1,0,0,values,4,&range));
  assert(!DecodeSourceFrame(family,&fixture.object,sizeof(T)-1,1,0,0,values,4,&range));
  assert(range.first==0 && range.count==2);
  assert(values[0].kind==0 && values[0].value==8 && values[1].kind==1 && values[1].value==17);
  assert(Decode(view,0,0,0,values,4,&range));assert(values[1].kind==2 && values[1].value==9);
  assert(Decode(view,1,0,1,values,4,&range));assert(range.first==2 && range.count==2);
  assert(values[0].kind==5 && values[0].value==7 && values[1].kind==-1); // omitted bit => None
  assert(!Decode(view,2,0,0,values,4,&range));
  assert(!Decode(view,1,0,2,values,4,&range));
  assert(!Decode(view,1,2,1,values,4,&range));
  assert(!Decode(view,1,0,0,nullptr,4,&range));
  assert(!Decode(view,1,0,0,values,1,&range));
  fixture.stack.Cell(1,64,7,UINT32_MAX);view=fixture.View();
  assert(!Decode(view,1,0,0,values,4,&range)); // missing map is not recoverable
  fixture.stack.Cell(1,64,7,3);fixture.stack.Cell(1,64,6,7);view=fixture.View();
  assert(!Decode(view,1,0,0,values,4,&range)); // mask index outside owned table
  fixture.stack.Cell(1,64,6,1);fixture.maps.Cell(3,8,0,7);view=fixture.View();
  assert(!Decode(view,1,0,0,values,4,&range)); // bad catalogue reference
  fixture.maps.Cell(3,8,0,3);fixture.catalog.Cell(3,16,0,6);view=fixture.View();
  assert(!Decode(view,1,0,0,values,4,&range)); // not an AOSP location kind
  fixture.catalog.Cell(3,16,0,1);fixture.inline_info.Cell(0,48,5,1);view=fixture.View();
  assert(!Decode(view,1,0,1,values,4,&range)); // cumulative count underflow
  fixture.inline_info.Cell(0,48,5,4);fixture.object.dex_maps.rows=3;view=fixture.View();
  assert(!Decode(view,1,0,0,values,4,&range)); // insufficient changed locations
  fixture.object.dex_maps.rows=4;fixture.object.dex_masks.region.bit_size=7;
  assert(!code_info_view::ReadDecoded(fixture.object,&view));
}
int main() {
  using code_info_view::SourceFamily;
  Check<abi::arm64::android11::CodeInfo>(SourceFamily::kAndroid11);
  Check<abi::arm64::android12::CodeInfo>(SourceFamily::kAndroid12);
  Check<abi::arm64::android13::CodeInfo>(SourceFamily::kAndroid13);
  Check<abi::arm64::android14::CodeInfo>(SourceFamily::kAndroid14);
  Check<abi::arm64::android16::CodeInfo>(SourceFamily::kAndroid16);
  Check<abi::arm64::android17::CodeInfo>(SourceFamily::kAndroid17);
  Fixture<abi::arm64::android13::CodeInfo> f;auto view=f.View();
  // Read raw mask bits above 32 without scalar truncation.
  Bits large_mask{70};large_mask.Raw(67,1,1);
  view.tables[5]={{large_mask.data.data(),0,70},1,70,1,{0,70}};
  bool set=false;assert(MaskBit(view.tables[5],0,67,&set) && set);
  assert(MaskBit(view.tables[5],0,66,&set) && !set);
  assert(!MaskBit(view.tables[5],0,70,&set));
  // Bound delta search by the fixed upstream distance; never claim a map after
  // reading arbitrarily far through malformed/foreign table storage.
  Bits long_stack{34*64};auto table=f.object.stack_maps;Bind(table,long_stack,34,8);
  for(unsigned row=0;row<34;++row) {
    long_stack.Cell(row,64,6,UINT32_MAX);long_stack.Cell(row,64,7,0);
  }
  f.object.stack_maps=table;view=f.View();Location locations[2];RegisterRange range;
  assert(!Decode(view,33,0,0,locations,2,&range));
  assert(Decode(view,32,0,0,locations,2,&range));assert(locations[0].kind==-1);
  puts("Decoded frame recovery: six fixed source declarations, delta maps, raw masks, inline counts and malformed maps passed");
}
