#include "platform/common/code_info_view.h"
#include <cassert>
#include <cstdio>
#include <array>
using namespace artdcevm;
using code_info_view::View;
using code_info_view::SourceFamily;
template<class T> void SourceFixture(SourceFamily family) {
  T info{};info.flags=3;info.dex_regs=4;
  View view;assert(code_info_view::ReadSourceObject(family,&info,sizeof(info),&view));
  assert(view.flags==3 && view.dex_regs==4);
  assert(!view.StackMaps().rows && !view.InlineInfos().rows);
  view.flags=123;
  assert(!code_info_view::ReadSourceObject(family,&info,sizeof(info)-1,&view));assert(view.flags==123);
  assert(!code_info_view::ReadSourceObject(family,nullptr,sizeof(info),&view));assert(view.flags==123);
  assert(!code_info_view::ReadSourceObject(family,&info,sizeof(info),nullptr));
}
int main() {
  SourceFixture<abi::arm64::android11::CodeInfo>(SourceFamily::kAndroid11);
  SourceFixture<abi::arm64::android12::CodeInfo>(SourceFamily::kAndroid12);
  SourceFixture<abi::arm64::android13::CodeInfo>(SourceFamily::kAndroid13);
  SourceFixture<abi::arm64::android17::CodeInfo>(SourceFamily::kAndroid17);
  abi::arm64::android11::CodeInfo old{};View view;
  assert(code_info_view::ReadDecoded(old,&view) && !view.code_size);
  abi::arm64::android17::CodeInfo modern{};modern.code_size=17;
  std::array<unsigned char,9000> bytes{};bytes[65534/8]|=1u<<(65534%8);
  modern.stack_maps.region={bytes.data(),0,70000};modern.stack_maps.rows=1;modern.stack_maps.row_bits=70000;
  for(size_t i=1;i<8;++i)modern.stack_maps.columns[i]=65534;
  assert(code_info_view::ReadDecoded(modern,&view) && view.code_size==17);
  uint32_t value;
  // Column 0 is a mask wider than uint32: it remains representable as a view,
  // while scalar reads correctly reject it. The last column crosses uint16.
  assert(!view.StackMaps().Read(0,0,&value));
  assert(view.StackMaps().columns[8]==70000);
  modern.stack_maps.row_bits=65535;
  assert(code_info_view::ReadDecoded(modern,&view));
  assert(view.StackMaps().Read(0,7,&value) && value==0);
  modern.stack_maps.columns[0]=1;view.flags=123;
  assert(!code_info_view::ReadDecoded(modern,&view) && view.flags==123);
  abi::arm64::android13::CodeInfo info{};info.dex_regs=4;
  std::array<unsigned char,3> stack{};std::array<unsigned char,2> inlines{};
  info.stack_maps.region={stack.data(),0,22};info.stack_maps.rows=1;
  uint16_t columns[]={0,1,2,3,4,5,13,14,22};
  for(size_t i=0;i<9;++i)info.stack_maps.columns[i]=columns[i];
  // start inline index 0 / register-map index 0 both encode as one.
  stack[5/8]|=1u<<(5%8);stack[14/8]|=1u<<(14%8);
  info.inline_infos.region={inlines.data(),0,13};info.inline_infos.rows=1;
  for(size_t i=0;i<6;++i)info.inline_infos.columns[i]=i;
  info.inline_infos.columns[6]=13;
  // Cumulative register end 8 encodes as 9, beginning at bit 5.
  inlines[0]=static_cast<unsigned char>(9u<<5);inlines[1]=9u>>3;
  assert(code_info_view::ReadDecoded(info,&view));
  assert(code_info_view::RegisterCount(view,0,0,0,&value) && value==4);
  assert(code_info_view::RegisterCount(view,0,0,1,&value) && value==4);
  value=123;assert(!code_info_view::RegisterCount(view,0,0,2,&value) && value==123);
  assert(!code_info_view::RegisterCount(view,1,0,0,&value) && value==123);
  assert(!code_info_view::RegisterCount(view,0,0,0,nullptr));
  stack[14/8]&=~(1u<<(14%8));assert(code_info_view::ReadDecoded(info,&view));
  assert(code_info_view::RegisterCount(view,0,0,1,&value) && value==0);
  info.stack_maps.columns[6]=4;view.flags=123;
  assert(!code_info_view::ReadDecoded(info,&view) && view.flags==123);
  info.stack_maps.columns[6]=13;info.stack_maps.region.bit_size=21;
  assert(!code_info_view::ReadDecoded(info,&view) && view.flags==123);
  info.stack_maps.region.bit_size=22;info.stack_maps.region.data=nullptr;
  assert(!code_info_view::ReadDecoded(info,&view) && view.flags==123);
  info.stack_maps.region.data=stack.data();info.stack_maps.region.bit_start=SIZE_MAX;
  assert(!code_info_view::ReadDecoded(info,&view) && view.flags==123);
  assert(!code_info_view::ReadSourceObject(static_cast<SourceFamily>(99),&info,sizeof(info),&view));
  assert(view.flags==123);
  puts("CodeInfo view: four object ABI families, modern masks, bounded tables and inline map counts passed");
}
