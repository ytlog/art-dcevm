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
  for(const auto& layout:{g::kLegacy,g::k16,g::k17}) {
    g::Identity value{};
    assert(g::ReadIdentity(layout,bytes.data(),bytes.size(),&value));
    assert(value.dex_cache==0x12345678);
    assert(value.access_flags==(layout.size==120?0x11112222:0x5555aaaa));
    assert(value.class_def_index==(layout.size==120?65538:131076) && value.dex_type_index==(layout.size==120?9:13));
    assert(layout.size==(layout.size==120?120:layout.size==112?112:108));
    g::Identity sentinel{1,2,3,4};
    assert(!g::ReadIdentity(layout,bytes.data(),layout.size-1,&sentinel));
    assert(sentinel.dex_cache==1 && sentinel.access_flags==2 && sentinel.class_def_index==3 && sentinel.dex_type_index==4);
  }
  for(const auto& layout:{g::kLegacy,g::k16,g::k17}) {
    const size_t primitive=layout.primitive_type,status=layout.status;
    // High primitive bits encode size shift and do not make kPrimNot primitive.
    for(uint32_t state=0;state<16;++state) {
      put(primitive,0xabcd0000);put(status,(state<<28)|0x0abcdef0);
      g::State value{};
      assert(g::ReadState(layout,bytes.data(),120,&value));
      assert(value.status==state && !value.IsPrimitive() && value.primitive_type==0xabcd0000);
      put(primitive,0xabcd0006);
      assert(g::ReadState(layout,bytes.data(),120,&value) && value.IsPrimitive());
    }
    g::State saved{1,2,3};
    assert(!g::ReadState(layout,bytes.data(),layout.size-1,&saved));
    assert(saved.access_flags==1 && saved.primitive_type==2 && saved.status==3);
  }
  g::State saved{1,2,3};
  assert(!g::ReadState(g::kLegacy,bytes.data()+1,120,&saved));
  assert(!g::ReadState(g::kLegacy,nullptr,120,&saved));
  assert(!g::ReadState(g::kLegacy,reinterpret_cast<const void*>(UINTPTR_MAX-3),120,&saved));
  assert(!g::ReadState(g::kLegacy,bytes.data(),120,nullptr));
  assert(saved.access_flags==1 && saved.status==3);
  g::Identity sentinel{1,2,3,4};
  assert(!g::ReadIdentity(g::kLegacy,bytes.data()+1,120,&sentinel));
  assert(!g::ReadIdentity(g::kLegacy,nullptr,120,&sentinel));
  assert(!g::ReadIdentity(g::kLegacy,reinterpret_cast<const void*>(UINTPTR_MAX-3),120,&sentinel));
  assert(!g::ReadIdentity(g::kLegacy,bytes.data(),120,nullptr));
  assert(sentinel.class_def_index==3 && sentinel.access_flags==2);
  auto bad=g::kLegacy;bad.status=bad.size;
  assert(!g::ReadState(bad,bytes.data(),120,&saved));
  bad=g::kLegacy;bad.access_flags=65;
  assert(!g::ReadIdentity(bad,bytes.data(),120,&sentinel));
  put(72,UINT32_MAX);g::Identity no_definition{};
  assert(g::ReadIdentity(g::k17,bytes.data(),120,&no_definition) && no_definition.class_def_index==-1);
  put(104,14u<<28);
  auto* status=reinterpret_cast<uint32_t*>(bytes.data()+104);
  auto* flags=reinterpret_cast<uint32_t*>(bytes.data()+56);
  std::thread writer([&]{for(unsigned i=0;i<200000;++i){
    __atomic_store_n(flags,0x5555aaaa,__ATOMIC_RELAXED);
    __atomic_store_n(flags,0xaaaa5555,__ATOMIC_RELAXED);
    __atomic_store_n(status,(12u<<28)|0x1234567,__ATOMIC_SEQ_CST);
    __atomic_store_n(status,(14u<<28)|0x7654321,__ATOMIC_SEQ_CST);
  }});
  for(unsigned i=0;i<200000;++i) {
    g::Identity value{};assert(g::ReadIdentity(g::k16,bytes.data(),120,&value));
    assert(value.access_flags==0x5555aaaa || value.access_flags==0xaaaa5555);
    g::State state{};assert(g::ReadState(g::k16,bytes.data(),120,&state));
    assert((state.access_flags==0x5555aaaa || state.access_flags==0xaaaa5555) && (state.status==12 || state.status==14));
  }
  writer.join();
  std::cout<<"Class metadata: fixed source source families, full signed DEX indices, bounded opaque references, primitive size bits, status nibble and concurrent scalar loads passed\n";
}
