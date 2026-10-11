#include "platform/common/native_selective_interpretation.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>
using namespace artdcevm;
template<class M>void Check(const method_metadata::Layout&layout){
 struct alignas(8) Array {uint32_t count=2,padding=0;M methods[2]{};} array;
 auto&method=array.methods[0];method.access_flags=9;method.dex_method_index=17;
 using namespace native_selective_interpretation;
 assert(detail::Identity(layout,&method,sizeof(method),17,9));
 assert(!detail::Identity(layout,&method,sizeof(method)-1,17,9));
 assert(!detail::Identity(layout,&method,sizeof(method),18,9));
 assert(!detail::Identity(layout,&method,sizeof(method),17,1));
 for(uint32_t forbidden:{0x100u,0x400u,0x40000u}){method.access_flags=9|forbidden;assert(!detail::Identity(layout,&method,sizeof(method),17,9|forbidden));}
 method.access_flags=9;method.dex_method_index=UINT32_MAX;assert(!detail::Identity(layout,&method,sizeof(method),UINT32_MAX,9));method.dex_method_index=17;
 assert(detail::Member(&array,sizeof(array),&array.methods[0],sizeof(M)));
 assert(detail::Member(&array,sizeof(array),&array.methods[1],sizeof(M)));
 M foreign{};assert(!detail::Member(&array,sizeof(array),&foreign,sizeof(M)));
 assert(!detail::Member(&array,sizeof(array),reinterpret_cast<unsigned char*>(&method)+4,sizeof(M)));
 assert(!detail::Member(&array,sizeof(array)-1,&method,sizeof(M)));
 assert(!detail::Member(&array,sizeof(array),&array, sizeof(M)));
 array.count=UINT32_MAX;assert(!detail::Member(&array,sizeof(array),&method,sizeof(M)));
 array.count=0;assert(!detail::Member(&array,sizeof(array),&method,sizeof(M)));
}
int main(){
 static_assert(!std::is_copy_constructible_v<native_selective_interpretation::Service>);
 Check<method_metadata::Method11>(method_metadata::kAndroid11);
 Check<method_metadata::Method12>(method_metadata::kAndroid12Onward);
 std::cout<<"Actual fixed Method11/Method12 identity and LenPrefixedArray: native/abstract/obsolete/runtime, public mismatch, foreign/unaligned/truncated membership rejected; no owner or activation permission simulated\n";
}
