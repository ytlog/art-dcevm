#include "platform/common/readonly_method_table.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace artdcevm::readonly_method_table;
void Check(Declaration source) {
  alignas(8) std::array<unsigned char,512> bytes{};
  uint32_t count=7;std::memcpy(bytes.data(),&count,4);
  std::vector<Oracle> items;
  const uint32_t flags[]={1,9,2,0x10001,0x10001,0x101};
  for(size_t i=0;i<6;++i){
    auto* method=bytes.data()+source.alignment+i*source.stride;
    std::memcpy(method+4,&flags[i],4);uint32_t id=10+3*i;
    std::memcpy(method+source.index_offset,&id,4);
    items.push_back({method,source.stride,id,flags[i]});
  }
  Proof proof{};std::string error;
  assert(Prove(source,bytes.data(),bytes.size(),items,6,4,&proof,&error));
  assert(proof.stride==source.stride&&proof.declared==6&&proof.total==7&&proof.direct==4);
  std::swap(items[0],items[5]);assert(Prove(source,bytes.data(),bytes.size(),items,6,4,&proof,&error));
  auto bad=items;bad.pop_back();assert(!Prove(source,bytes.data(),bytes.size(),bad,6,4,&proof,&error));
  // A missing direct declaration cannot silently become a complete oracle.
  bad=items;bad[1]=bad[0];assert(!Prove(source,bytes.data(),bytes.size(),bad,6,4,&proof,&error));
  bad=items;bad[0].index=bad[1].index;assert(!Prove(source,bytes.data(),bytes.size(),bad,6,4,&proof,&error));
  bad=items;bad[0].flags^=0x100;assert(!Prove(source,bytes.data(),bytes.size(),bad,6,4,&proof,&error));
  bad=items;bad[0].extent=source.stride-1;assert(!Prove(source,bytes.data(),bytes.size(),bad,6,4,&proof,&error));
  bad=items;bad[0].method=bytes.data()+source.alignment+6*source.stride;
  assert(!Prove(source,bytes.data(),bytes.size(),bad,6,4,&proof,&error));
  bad=items;bad[0].method=static_cast<const unsigned char*>(bad[0].method)+1;
  assert(!Prove(source,bytes.data(),bytes.size(),bad,6,4,&proof,&error));
  assert(!Prove(source,bytes.data(),source.alignment+6*source.stride,items,6,4,&proof,&error));
  assert(!Prove(source,bytes.data(),bytes.size(),items,6,3,&proof,&error));
  assert(!Prove(source,bytes.data()+1,bytes.size()-1,items,6,4,&proof,&error));
  assert(!Prove(source,bytes.data(),bytes.size(),{},6,4,&proof,&error));
  const Proof retained=proof;bad=items;bad[0].index=UINT32_MAX;
  assert(!Prove(source,bytes.data(),bytes.size(),bad,6,4,&proof,&error));assert(proof.total==retained.total);
}
int main(){Check({28,12,4});Check({24,8,4});Check({40,12,8});Check({32,8,8});
  std::cout<<"32/64 Method table: complete/permuted DEX oracle, copied tail and refusal fixtures passed\n";}
