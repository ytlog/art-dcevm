#include "platform/common/native_call_graph_dispatch.h"
#include <cassert>
#include <iostream>
#include <array>
using namespace artdcevm;
int main(){using namespace native_call_graph_dispatch;
 alignas(8)std::array<unsigned char,40>table{};uint64_t one=0x1000,two=0x2000;
 std::memcpy(table.data()+16,&one,8);std::memcpy(table.data()+24,&two,8);void*target=nullptr;
 assert(detail::Slot(table.data(),table.size(),16,2,0,&target)&&target==reinterpret_cast<void*>(one));
 assert(detail::Slot(table.data(),table.size(),16,2,1,&target)&&target==reinterpret_cast<void*>(two));
 assert(!detail::Slot(table.data(),31,16,2,0,&target));assert(!detail::Slot(table.data(),40,16,2,2,&target));
 assert(!detail::Slot(table.data(),40,SIZE_MAX,2,0,&target));assert(!detail::Slot(table.data(),40,16,SIZE_MAX,0,&target));
 one=0;std::memcpy(table.data()+16,&one,8);assert(!detail::Slot(table.data(),40,16,2,0,&target));
 one=0x1001;std::memcpy(table.data()+16,&one,8);assert(!detail::Slot(table.data(),40,16,2,0,&target));
 method_metadata::Method11 m11{};method_metadata::Method12 m12{};m11.method_index=9;m12.method_index=17;uint16_t slot=0;
 assert(detail::MethodSlot(method_metadata::kAndroid11,&m11,sizeof(m11),&slot)&&slot==9);
 assert(detail::MethodSlot(method_metadata::kAndroid12Onward,&m12,sizeof(m12),&slot)&&slot==17);
 assert(!detail::MethodSlot(method_metadata::kAndroid11,&m11,sizeof(m11)-1,&slot));
 auto bad=method_metadata::kAndroid11;bad.size=39;assert(!detail::MethodSlot(bad,&m11,sizeof(m11),&slot));
 std::cout<<"Bounded dispatch storage and fixed Method slot mechanics: PASS\n";
}
