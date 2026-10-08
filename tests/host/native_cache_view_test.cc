#include "platform/common/native_cache_view.h"
#include <array>
#include <cassert>
#include <iostream>
namespace n=artdcevm::native_cache;
int main() {
 alignas(16) std::array<uint64_t,2048> pairs{};
 std::array<std::atomic<uintptr_t>,2049> full{};
 alignas(8) artdcevm::abi::arm64::android11::LegacyCache legacy{};
 alignas(8) artdcevm::abi::arm64::android14::DualCache dual{};
 legacy.fields=legacy.methods=dual.fields=dual.methods=reinterpret_cast<uintptr_t>(pairs.data());
 legacy.num_fields=3;legacy.num_methods=5;
 dual.fields_array=dual.methods_array=reinterpret_cast<uintptr_t>(full.data());
 for(auto model:{artdcevm::dex_cache_metadata::Model::Pairs,artdcevm::dex_cache_metadata::Model::DualArrays})for(auto member:{n::Member::Fields,n::Member::Methods}) {
  const void* cache=model==artdcevm::dex_cache_metadata::Model::Pairs?static_cast<void*>(&legacy):static_cast<void*>(&dual);
  const size_t size=model==artdcevm::dex_cache_metadata::Model::Pairs?108:112;
  n::View view;
  assert(n::Read(model,cache,size,member,2049,&view));
  assert(view.pairs==pairs.data() && view.pair_count==(model==artdcevm::dex_cache_metadata::Model::Pairs?(member==n::Member::Fields?3u:5u):1024u));
  assert(view.full==(model==artdcevm::dex_cache_metadata::Model::Pairs?nullptr:full.data()) && view.full_count==(model==artdcevm::dex_cache_metadata::Model::Pairs?0u:2049u));
  n::View sentinel{pairs.data(),17,full.data(),23};
  assert(!n::Read(model,cache,size-1,member,2049,&sentinel));
  assert(sentinel.pair_count==17 && sentinel.full_count==23);
 }
 n::View sentinel{pairs.data(),17,full.data(),23};
 assert(!n::Read(static_cast<artdcevm::dex_cache_metadata::Model>(-1),&legacy,108,n::Member::Fields,0,&sentinel));
 assert(!n::Read(artdcevm::dex_cache_metadata::Model::Pairs,nullptr,108,n::Member::Fields,0,&sentinel));
 assert(!n::Read(artdcevm::dex_cache_metadata::Model::Pairs,reinterpret_cast<unsigned char*>(&legacy)+4,108,n::Member::Fields,0,&sentinel));
 assert(!n::Read(artdcevm::dex_cache_metadata::Model::Pairs,reinterpret_cast<void*>(UINTPTR_MAX-7),108,n::Member::Fields,0,&sentinel));
 assert(!n::Read(artdcevm::dex_cache_metadata::Model::Pairs,&legacy,108,n::Member(99),0,&sentinel));
 assert(!n::Read(artdcevm::dex_cache_metadata::Model::Pairs,&legacy,108,n::Member::Fields,0,nullptr));
 legacy.num_fields=1025;assert(!n::Read(artdcevm::dex_cache_metadata::Model::Pairs,&legacy,108,n::Member::Fields,0,&sentinel));
 legacy.num_fields=1;legacy.fields=0;assert(!n::Read(artdcevm::dex_cache_metadata::Model::Pairs,&legacy,108,n::Member::Fields,0,&sentinel));
 legacy.num_fields=0;n::View view;
 assert(n::Read(artdcevm::dex_cache_metadata::Model::Pairs,&legacy,108,n::Member::Fields,0,&view) && !view.pairs && !view.pair_count);
 for(uint64_t bad:{uint64_t(UINTPTR_MAX&~uintptr_t{15}),uint64_t(reinterpret_cast<uintptr_t>(pairs.data())+8)}) {
  dual.fields=bad;assert(!n::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,n::Member::Fields,0,&sentinel));
 }
 dual.fields=0;
 assert(n::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,n::Member::Fields,2049,&view) && !view.pair_count && view.full_count==2049);
 dual.fields_array=UINTPTR_MAX&~uintptr_t{7};assert(!n::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,n::Member::Fields,2,&sentinel));
 dual.fields_array=reinterpret_cast<uintptr_t>(full.data())+4;assert(!n::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,n::Member::Fields,2,&sentinel));
 assert(sentinel.pairs==pairs.data() && sentinel.pair_count==17 && sentinel.full==full.data() && sentinel.full_count==23);
 dual.fields_array=0;assert(n::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,n::Member::Fields,0,&view) && !view.pair_count && !view.full_count);
 dual.fields=reinterpret_cast<uintptr_t>(pairs.data());
 assert(n::Read(artdcevm::dex_cache_metadata::Model::DualArrays,&dual,112,n::Member::Fields,0,&view) && view.pair_count==1024 && !view.full_count);
 std::cout<<"Native cache views: fixed source field/method identities, both representations, full-only/pair-only/empty, counts/bounds and unchanged failure output passed\n";
}
