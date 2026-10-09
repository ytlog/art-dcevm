#include "platform/common/readonly_field_table.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace artdcevm::readonly_field_table;
int main(){
 alignas(4) std::array<unsigned char,52> merged{};
 uint32_t count=3;std::memcpy(merged.data(),&count,4);
 std::vector<Oracle> fields;
 for(uint32_t i=0;i<3;++i){Field f{0x12340000,i==0?9u:(i==1?2u:1u),10+i,16+i*4};
  std::memcpy(merged.data()+4+16*i,&f,16);fields.push_back({merged.data()+4+16*i,16,10+i,f.flags});}
 std::string error;assert(Prove({{merged.data(),merged.size(),3}},fields,3,1,0x12340000,&error));
 auto bad=fields;bad.pop_back();assert(!Prove({{merged.data(),merged.size(),3}},bad,3,1,0x12340000,&error));
 bad=fields;bad[2]=bad[1];assert(!Prove({{merged.data(),merged.size(),3}},bad,3,1,0x12340000,&error));
 bad=fields;bad[0].index=50;assert(!Prove({{merged.data(),merged.size(),3}},bad,3,1,0x12340000,&error));
 bad=fields;bad[0].extent=15;assert(!Prove({{merged.data(),merged.size(),3}},bad,3,1,0x12340000,&error));
 assert(!Prove({{merged.data(),merged.size()-1,3}},fields,3,1,0x12340000,&error));
 assert(!Prove({{merged.data(),merged.size(),2}},fields,3,1,0x12340000,&error));
 assert(!Prove({{merged.data(),merged.size(),3}},fields,3,0,0x12340000,&error));
 assert(!Prove({{merged.data(),merged.size(),3}},fields,3,1,0x12340004,&error));
 assert(!Prove({{merged.data(),merged.size(),3,0}},fields,3,1,0x12340000,&error));
 // Split source arrays have independent static/instance allocation boundaries.
 alignas(4) std::array<unsigned char,20> statics{};alignas(4) std::array<unsigned char,36> instances{};
 count=1;std::memcpy(statics.data(),&count,4);std::memcpy(statics.data()+4,merged.data()+4,16);
 count=2;std::memcpy(instances.data(),&count,4);std::memcpy(instances.data()+4,merged.data()+20,32);
 fields[0].field=statics.data()+4;fields[1].field=instances.data()+4;fields[2].field=instances.data()+20;
 assert(Prove({{instances.data(),instances.size(),2,0},{statics.data(),statics.size(),1,1}},fields,3,1,0x12340000,&error));
 assert(!Prove({{instances.data(),instances.size(),2,1},{statics.data(),statics.size(),1,0}},fields,3,1,0x12340000,&error));
 std::cout<<"32/64 Field tables: merged/split complete oracle, owner/flags/index/extents and partition refusals passed\n";
}
