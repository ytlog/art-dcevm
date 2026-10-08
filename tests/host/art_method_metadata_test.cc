#include "platform/art_method_metadata.h"
#include <array>
#include <cassert>
#include <iostream>
#include <thread>
namespace g=artdcevm::method_metadata;
int main() {
  // Golden serialized fields from the fixed source order, independent of the
  // helper's offset selection. API 30's code offset must never become its ID.
  alignas(8) std::array<unsigned char,40> bytes{};
  auto put=[&](size_t offset,uint32_t value){std::memcpy(bytes.data()+offset,&value,4);};
  put(4,0x12345678);put(8,111);put(12,222);
  for(const auto& source:{g::kAndroid11,g::kAndroid12Onward}) {
    g::Identity value{};
    assert(g::ReadIdentity(source,bytes.data(),bytes.size(),&value));
    assert(value.access_flags==0x12345678 && value.dex_method_index==(source.size==40?222:111));
    const auto* layout=&source;
    assert(layout->size==(source.size==40?40:32) && layout->data==(source.size==40?24:16) && layout->entry_point==(source.size==40?32:24));
    g::Identity sentinel{44,55};
    assert(!g::ReadIdentity(source,bytes.data(),layout->size-1,&sentinel));
    assert(sentinel.access_flags==44 && sentinel.dex_method_index==55);
  }
  g::Identity sentinel{44,55};
  assert(!g::ReadIdentity(g::kAndroid12Onward,bytes.data()+1,39,&sentinel));
  assert(!g::ReadIdentity(g::kAndroid12Onward,nullptr,40,&sentinel));
  assert(!g::ReadIdentity(g::kAndroid12Onward,reinterpret_cast<const void*>(UINTPTR_MAX-3),40,&sentinel));
  assert(!g::ReadIdentity(g::kAndroid12Onward,bytes.data(),40,nullptr));
  assert(sentinel.access_flags==44 && sentinel.dex_method_index==55);
  auto malformed=g::kAndroid12Onward;
  malformed.dex_method_index=malformed.size;
  assert(!g::ReadIdentity(malformed,bytes.data(),bytes.size(),&sentinel));
  malformed.size=0;
  assert(!g::ReadIdentity(malformed,bytes.data(),bytes.size(),&sentinel));
  assert(sentinel.access_flags==44 && sentinel.dex_method_index==55);
  assert(g::ReadIdentity(g::kAndroid12Onward,bytes.data(),bytes.size(),&sentinel));
  assert(sentinel.dex_method_index==111);
  auto* flags=reinterpret_cast<uint32_t*>(bytes.data()+4);
  __atomic_store_n(flags,0x12345678,__ATOMIC_RELAXED);
  std::thread writer([&]{for(unsigned i=0;i<200000;++i){
    __atomic_store_n(flags,0x12345678,__ATOMIC_RELAXED);
    __atomic_store_n(flags,0x87654321,__ATOMIC_RELAXED);
  }});
  for(unsigned i=0;i<200000;++i) {
    g::Identity value{};
    assert(g::ReadIdentity(g::kAndroid12Onward,bytes.data(),bytes.size(),&value));
    assert((value.access_flags==0x12345678 || value.access_flags==0x87654321) && value.dex_method_index==111);
  }
  writer.join();
  std::cout<<"ArtMethod metadata: fixed source source layouts, bounds/alignment/output preservation and concurrent atomic flags passed\n";
}
