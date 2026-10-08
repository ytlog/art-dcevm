#include "platform/common/type_cache_update.h"
#include <cassert>
#include <array>
#include <iostream>
#include <memory>
#include <thread>
namespace g=artdcevm::type_cache;
int main() {
  auto pairs=std::make_unique<g::Pair[]>(1024);
  alignas(8) std::array<uint32_t,3> full{};
  alignas(8) g::Legacy legacy{};alignas(8) g::Dual dual{};
  legacy.types=reinterpret_cast<uintptr_t>(pairs.get());legacy.num_types=3;
  dual.types=reinterpret_cast<uintptr_t>(pairs.get());dual.types_array=reinterpret_cast<uintptr_t>(full.data());
  for(auto model:{artdcevm::dex_cache_metadata::Model::Pairs,artdcevm::dex_cache_metadata::Model::DualArrays}) {
    for(uint32_t i=0;i<1024;++i)pairs[i]={i<2?7u:0u,i<2?i:0u};
    full={7,0,9};
    g::View view;
    assert(g::Read(model,model==artdcevm::dex_cache_metadata::Model::Pairs?static_cast<void*>(&legacy):static_cast<void*>(&dual),model==artdcevm::dex_cache_metadata::Model::Pairs?108:112,3,&view));
    assert(view.pair_count==(model==artdcevm::dex_cache_metadata::Model::Pairs?3u:1024u) && view.full_count==(model==artdcevm::dex_cache_metadata::Model::Pairs?0u:3u));
    unsigned calls=0;
    auto changed=view.Apply([&](uint32_t* opaque){++calls;if(*opaque!=7)return false;*opaque=11;return true;});
    assert(changed==(model==artdcevm::dex_cache_metadata::Model::Pairs?2u:3u) && calls==view.pair_count+view.full_count);
    assert(pairs[0].object==11 && pairs[0].index==0 && pairs[1].index==1 && pairs[2].index==0 && !pairs[2].object);
    assert(full[0]==(model==artdcevm::dex_cache_metadata::Model::Pairs?7u:11u) && !full[1] && full[2]==9);
    // A barrier may change its local shadow without requesting a persistent update.
    assert(!view.Apply([](uint32_t* shadow){*shadow=99;return false;}));
    assert(pairs[0].object==11 && pairs[0].index==0);
    g::View sentinel{nullptr,17,nullptr,23};
    assert(!g::Read(model,model==artdcevm::dex_cache_metadata::Model::Pairs?static_cast<void*>(&legacy):static_cast<void*>(&dual),model==artdcevm::dex_cache_metadata::Model::Pairs?107:111,3,&sentinel));
    assert(sentinel.pair_count==17 && sentinel.full_count==23);
  }
  g::View concurrent;
  assert(g::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,3,&concurrent));
  std::thread writer([&]{for(unsigned i=0;i<2000;++i) {
    concurrent.Apply([](uint32_t* value){if(*value==11){*value=7;return true;}if(*value==7){*value=11;return true;}return false;});
  }});
  for(unsigned i=0;i<200000;++i) {
    g::Pair value;__atomic_load(pairs.get(),&value,__ATOMIC_ACQUIRE);
    assert((value.object==7 || value.object==11) && value.index==0);
    const auto root=__atomic_load_n(full.data(),__ATOMIC_RELAXED);assert(root==7 || root==11);
  }
  writer.join();
  dual.types=0;
  g::View full_only;assert(g::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,3,&full_only));
  assert(!full_only.pair_count && full_only.full_count==3);
  unsigned full_calls=0;full_only.Apply([&](uint32_t*){++full_calls;return false;});assert(full_calls==3);
  dual.types=reinterpret_cast<uintptr_t>(pairs.get());dual.types_array=0;
  g::View pairs_only;assert(g::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,3,&pairs_only));
  assert(pairs_only.pair_count==1024 && !pairs_only.full_count);
  legacy.types=0;g::View absent;assert(g::Read(artdcevm::dex_cache_metadata::Model::Pairs,&legacy,108,0,&absent));
  assert(!absent.pair_count && !absent.Apply([](uint32_t*){assert(false);return true;}));
  legacy.types=reinterpret_cast<uintptr_t>(pairs.get());
  g::View sentinel{nullptr,17,nullptr,23};
  assert(!g::Read(static_cast<artdcevm::dex_cache_metadata::Model>(-1),&legacy,108,0,&sentinel));
  assert(!g::Read(artdcevm::dex_cache_metadata::Model::Pairs,nullptr,108,0,&sentinel));
  assert(!g::Read(artdcevm::dex_cache_metadata::Model::Pairs,reinterpret_cast<unsigned char*>(&legacy)+4,108,0,&sentinel));
  assert(!g::Read(artdcevm::dex_cache_metadata::Model::Pairs,reinterpret_cast<void*>(UINTPTR_MAX-7),108,0,&sentinel));
  assert(!g::Read(artdcevm::dex_cache_metadata::Model::Pairs,&legacy,108,0,nullptr));
  legacy.num_types=1025;assert(!g::Read(artdcevm::dex_cache_metadata::Model::Pairs,&legacy,108,0,&sentinel));legacy.num_types=3;
  for(uint64_t bad:{uint64_t{UINTPTR_MAX&~uintptr_t{7}},uint64_t{reinterpret_cast<uintptr_t>(pairs.get())+4}}) {
    dual.types=bad;assert(!g::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,3,&sentinel));
    assert(sentinel.pair_count==17 && sentinel.full_count==23);
  }
  dual.types=0;dual.types_array=UINTPTR_MAX&~uintptr_t{3};
  assert(!g::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,3,&sentinel));
  dual.types_array=0;g::View empty;
  assert(g::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,3,&empty) && !empty.Apply([](uint32_t*){assert(false);return true;}));
  std::cout<<"Type cache update: fixed source opaque roots, both dual representations, index preservation, local shadow, atomic reader/update publication, empty and bounds passed\n";
}
