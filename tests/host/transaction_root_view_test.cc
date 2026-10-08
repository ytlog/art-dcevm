#include "platform/common/transaction_root_view.h"
#include <cassert>
#include <cstdio>
using namespace artdcevm::transaction_root;
int main() {
  alignas(8) unsigned char owner_storage[72*65]{};
  Owners owners{owner_storage,owner_storage+sizeof(owner_storage),owner_storage+sizeof(owner_storage)};
  uint32_t handle=0;
  uint64_t initialized[2]{1,1},actual[2]{2,1},initial[2]{4,0};
  Holder holder{&handle,&owners,{initialized,65,2},{actual,65,2},{initial,65,2}};
  Holder parsed;
  assert(ReadHolderHeader(&holder,sizeof(holder),&parsed));
  assert(parsed.array_handle==&handle && parsed.initialized.begin==initialized);
  Iterator iterator{64,&holder},copy;
  assert(ReadIterator(&iterator,sizeof(iterator),&copy) && copy.index==64 && copy.holder==&holder);
  for(auto family : {OwnerFamily::kAndroid11,OwnerFamily::kAndroid13,OwnerFamily::kAndroid15}) {
    Metadata metadata;assert(ReadMetadata(parsed,65,family,&metadata));
    Roots roots;size_t calls=0;
    auto read=[&](size_t slot)->void* {++calls;return reinterpret_cast<void*>(uintptr_t(0x1000+slot));};
    assert(ReadRoots(metadata,64,read,&roots));
    assert(calls==6 && roots.owner==owner_storage+64*72 && roots.initialized && roots.actually_structural && !roots.initial_structural);
    assert(roots.values[0]==reinterpret_cast<void*>(0x1000+64*13+3));
    assert(roots.values[5]==reinterpret_cast<void*>(0x1000+64*13+12));
    assert(ReadRoots(metadata,0,read,&roots));
    assert(roots.initialized && !roots.actually_structural && !roots.initial_structural);
    roots.index=123;calls=0;
    assert(!ReadRoots(metadata,65,read,&roots) && calls==0 && roots.index==123);
    assert(ReadRoots(metadata,1,[](size_t)->void* {return nullptr;},&roots));
    for(auto* value:roots.values)assert(!value);
  }
  Metadata preserved;preserved.count=123;
  assert(!ReadMetadata(holder,65,static_cast<OwnerFamily>(99),&preserved) && preserved.count==123);
  holder.initialized.capacity=1;
  assert(!ReadMetadata(holder,65,OwnerFamily::kAndroid15,&preserved) && preserved.count==123);
  holder.initialized.capacity=2;holder.initialized.size=64;
  assert(!ReadMetadata(holder,65,OwnerFamily::kAndroid15,&preserved));
  holder.initialized.size=65;owners.end=owner_storage+sizeof(owner_storage)-1;
  assert(!ReadMetadata(holder,65,OwnerFamily::kAndroid15,&preserved));
  owners.end=owner_storage+sizeof(owner_storage);owners.capacity=owners.end-72;
  assert(!ReadMetadata(holder,65,OwnerFamily::kAndroid15,&preserved));
  owners.capacity=owners.end;assert(!ReadMetadata(holder,64,OwnerFamily::kAndroid15,&preserved));
  holder.initialized.begin=reinterpret_cast<const uint64_t*>(uintptr_t(SIZE_MAX-7));
  assert(!ReadMetadata(holder,65,OwnerFamily::kAndroid15,&preserved));
  Holder sentinel{};sentinel.redefinitions=reinterpret_cast<void*>(0x1234);
  assert(!ReadHolderHeader(&holder,sizeof(holder)-1,&sentinel));assert(sentinel.redefinitions==reinterpret_cast<void*>(0x1234));
  assert(!ReadHolderHeader(reinterpret_cast<const unsigned char*>(&holder)+1,sizeof(holder),&sentinel));
  iterator.index=-1;copy.index=123;assert(!ReadIterator(&iterator,sizeof(iterator),&copy) && copy.index==123);
  iterator.index=0;iterator.holder=nullptr;assert(!ReadIterator(&iterator,sizeof(iterator),&copy));
  Owners empty{};Holder empty_holder{&handle,&empty,{nullptr,0,0},{nullptr,0,0},{nullptr,0,0}};
  assert(ReadMetadata(empty_holder,0,OwnerFamily::kAndroid11,&preserved));
  assert(!ReadRoots(preserved,0,[](size_t)->void* {assert(false);return nullptr;},nullptr));
  puts("Transaction roots: source owner families, 65-bit vectors, root slots, empty and malformed boundaries passed");
}
