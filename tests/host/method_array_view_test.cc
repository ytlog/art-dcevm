#include "platform/method_array_view.h"
#include <array>
#include <cassert>
#include <iostream>
namespace g=artdcevm::method_array;
int main() {
  alignas(8) std::array<unsigned char,120> klass{};
  alignas(8) std::array<unsigned char,208> array{};
  auto put=[&](size_t offset,uint32_t n){std::memcpy(array.data()+offset,&n,4);};
  auto half=[&](size_t offset,uint16_t n){std::memcpy(klass.data()+offset,&n,2);};
  half(116,4);half(118,2);half(108,3);half(110,1);
  for(int api=30;api<=36;++api) {
    put(0,5);
    g::Boundaries b{};
    assert(g::ReadBoundaries(api,klass.data(),klass.size(),5,g::Kind::Ordinary,nullptr,&b));
    assert(b.direct_count==(api==36?1u:2u) && b.declared_count==(api==36?3u:4u));
    assert(b.order==g::Order::DirectThenVirtual);
    g::View v;
    const size_t stride=api==30?40:32;
    assert(g::Read(api,array.data(),8+5*stride,b,&v));
    assert(v.count==5 && v.stride==stride && v.At(4)==array.data()+8+4*stride && !v.At(5));
    for(uint32_t i=0;i<5;++i)assert(v.IsDirect(i)==(i<b.direct_count));
    uint32_t visited=0;
    assert(!v.VisitVirtual([&](void* method){assert(method==v.At(b.direct_count+visited));++visited;return false;}));
    assert(visited==5-b.direct_count);
    visited=0;
    assert(v.VisitVirtual([&](void* method){assert(method==v.At(b.direct_count));++visited;return true;}));
    assert(visited==1);
    uint64_t address=reinterpret_cast<uintptr_t>(array.data());
    std::memcpy(klass.data()+48,&address,8);
    g::View trusted;
    assert(g::ReadTrustedClass(api,klass.data(),klass.size(),g::Kind::Ordinary,nullptr,&trusted));
    assert(trusted.count==v.count && trusted.stride==v.stride);
    g::View sentinel{nullptr,11,13,{1,2,g::Order::ByFlags}};
    assert(!g::Read(api,array.data(),8+5*stride-1,b,&sentinel));
    assert(sentinel.count==11 && sentinel.stride==13);
  }
  // Source order can interleave virtual/static/private/constructor declarations.
  put(0,6);
  const uint32_t flags[]={1,8,1,2,0x10000,0x1000008};
  for(size_t i=0;i<6;++i)put(8+32*i+4,flags[i]);
  artdcevm::evolution::MappedDexMemberCounts dex{0,0,3,2};
  g::Boundaries b{};
  assert(g::ReadBoundaries(37,klass.data(),108,6,g::Kind::Ordinary,&dex,&b));
  assert(b.direct_count==3 && b.declared_count==5 && b.order==g::Order::ByFlags);
  g::View v;
  assert(g::Read(37,array.data(),array.size(),b,&v));
  for(uint32_t i=0;i<6;++i)assert(v.IsDirect(i)==(i==1 || i==3 || i==4));
  const uint32_t virtuals[]={0,2,5};uint32_t visited=0;
  assert(!v.VisitVirtual([&](void* method){assert(method==v.At(virtuals[visited++]));return false;}));
  assert(visited==3);
  g::View trusted;
  assert(g::ReadTrustedClass(37,klass.data(),108,g::Kind::Ordinary,&dex,&trusted));
  assert(trusted.boundaries.declared_count==5 && trusted.count==6);
  g::View held{nullptr,11,13,{1,2,g::Order::ByFlags}};
  assert(!g::ReadTrustedClass(37,klass.data(),108,g::Kind::Ordinary,nullptr,&held));
  assert(!g::ReadTrustedClass(37,klass.data(),107,g::Kind::Ordinary,&dex,&held));
  assert(!g::ReadTrustedClass(37,klass.data()+1,108,g::Kind::Ordinary,&dex,&held));
  assert(!g::ReadTrustedClass(37,nullptr,108,g::Kind::Ordinary,&dex,&held));
  assert(!g::ReadTrustedClass(37,klass.data(),108,g::Kind::Ordinary,&dex,nullptr));
  assert(held.count==11 && held.stride==13);
  for(uint64_t bad:{uint64_t{UINTPTR_MAX-7},uint64_t{reinterpret_cast<uintptr_t>(array.data())+1}}) {
    std::memcpy(klass.data()+48,&bad,8);
    assert(!g::ReadTrustedClass(37,klass.data(),108,g::Kind::Ordinary,&dex,&held));
    assert(held.count==11 && held.stride==13);
  }
  uint64_t empty=0;std::memcpy(klass.data()+48,&empty,8);
  assert(!g::ReadTrustedClass(37,klass.data(),108,g::Kind::Ordinary,&dex,&held));
  artdcevm::evolution::MappedDexMemberCounts no_methods{};
  assert(g::ReadTrustedClass(37,klass.data(),108,g::Kind::Ordinary,&no_methods,&held));
  assert(!held.count && !held.VisitVirtual([](void*){assert(false);return true;}));
  assert(!g::ReadTrustedClass(29,klass.data(),120,g::Kind::Primitive,nullptr,&held));
  assert(!g::ReadTrustedClass(37,reinterpret_cast<void*>(UINTPTR_MAX-3),108,g::Kind::Primitive,nullptr,&held));
  uint64_t restored=reinterpret_cast<uintptr_t>(array.data());std::memcpy(klass.data()+48,&restored,8);
  // The obsolete offset bytes above must not affect Android 17 boundaries.
  assert(g::ReadBoundaries(37,klass.data(),108,6,g::Kind::Proxy,nullptr,&b));
  assert(b.direct_count==1 && b.declared_count==6);
  for(auto kind:{g::Kind::Array,g::Kind::Primitive}) {
    assert(g::ReadBoundaries(37,klass.data(),108,6,kind,nullptr,&b));
    assert(!b.direct_count && !b.declared_count);
  }
  g::Boundaries sentinel{11,22,g::Order::DirectThenVirtual};
  assert(!g::ReadBoundaries(37,klass.data(),108,6,g::Kind::Ordinary,nullptr,&sentinel));
  dex.direct_methods=UINT32_MAX;
  assert(!g::ReadBoundaries(37,klass.data(),108,6,g::Kind::Ordinary,&dex,&sentinel));
  dex.direct_methods=3;dex.virtual_methods=4;
  assert(!g::ReadBoundaries(37,klass.data(),108,6,g::Kind::Ordinary,&dex,&sentinel));
  assert(!g::ReadBoundaries(37,klass.data(),108,0,g::Kind::Proxy,nullptr,&sentinel));
  assert(!g::ReadBoundaries(37,nullptr,108,6,g::Kind::Primitive,nullptr,&sentinel));
  assert(!g::ReadBoundaries(37,klass.data(),108,6,g::Kind::Primitive,nullptr,nullptr));
  assert(!g::ReadBoundaries(37,klass.data(),107,6,g::Kind::Primitive,nullptr,&sentinel));
  assert(!g::ReadBoundaries(37,klass.data()+1,108,6,g::Kind::Primitive,nullptr,&sentinel));
  assert(!g::ReadBoundaries(37,reinterpret_cast<void*>(UINTPTR_MAX-3),108,6,g::Kind::Primitive,nullptr,&sentinel));
  for(int api:{29,38})assert(!g::ReadBoundaries(api,klass.data(),120,6,g::Kind::Primitive,nullptr,&sentinel));
  half(118,5);half(116,4);
  assert(!g::ReadBoundaries(33,klass.data(),120,6,g::Kind::Ordinary,nullptr,&sentinel));
  assert(sentinel.direct_count==11 && sentinel.declared_count==22);
  g::View unchanged{nullptr,11,13,{1,2,g::Order::ByFlags}};
  assert(!g::Read(37,array.data()+1,207,{0,0,g::Order::ByFlags},&unchanged));
  assert(!g::Read(37,array.data(),7,{0,0,g::Order::ByFlags},&unchanged));
  assert(!g::Read(37,array.data(),208,{0,7,g::Order::ByFlags},&unchanged));
  assert(!g::Read(37,array.data(),208,{2,1,g::Order::ByFlags},&unchanged));
  assert(!g::Read(37,array.data(),208,{1,2,g::Order::DirectThenVirtual},&unchanged));
  assert(!g::Read(33,array.data(),208,{1,2,g::Order::ByFlags},&unchanged));
  assert(!g::Read(37,nullptr,0,{0,1,g::Order::ByFlags},&unchanged));
  assert(!g::Read(37,nullptr,8,{0,0,g::Order::ByFlags},&unchanged));
  assert(!g::Read(37,reinterpret_cast<void*>(UINTPTR_MAX-7),8,{0,0,g::Order::ByFlags},&unchanged));
  assert(unchanged.count==11 && unchanged.stride==13);
  assert(g::Read(37,nullptr,0,{0,0,g::Order::ByFlags},&v));
  assert(v.count==0 && !v.At(0) && !v.IsDirect(0));
  std::cout<<"Method arrays: API30-37 stride/partitions, interleaved direct flags, copied exclusion, proxy/array/primitive counts, virtual traversal/early stop, trusted class bounds and bounded failures passed\n";
}
