#include "platform/common/source_cache_field_witness.h"
#include <cassert>
#include <iostream>
using artdcevm::source_cache_field_witness::Check;
int main(){
 void* klass=reinterpret_cast<void*>(0x1234);std::string error;
 // Actual AOSP hidden_api.cc MaybeUpdateAccessFlags adds kAccPublicApi and
 // kAccCorePlatformApi; all public language modifiers remain independently exact.
 for(uint32_t flags:{0x41u,0x10000041u,0x30000041u})
  assert(Check(klass,klass,"number","I",7,7,flags,0x41,&error));
 assert(!Check(klass,reinterpret_cast<void*>(0x5678),"number","I",7,7,0x10000041,0x41,&error)&&error.find("declaring Class")!=std::string::npos);
 assert(!Check(klass,klass,"other","I",7,7,0x10000041,0x41,&error)&&error.find("DEX name")!=std::string::npos);
 assert(!Check(klass,klass,"number","J",7,7,0x10000041,0x41,&error)&&error.find("DEX descriptor")!=std::string::npos);
 assert(!Check(klass,klass,"number","I",8,7,0x10000041,0x41,&error)&&error.find("FieldId")!=std::string::npos);
 assert(!Check(klass,klass,"number","I",7,7,0x10000041,0x49,&error)&&error.find("public modifiers")!=std::string::npos);
 for(uint32_t extra:{0x8u,0x10u,0x80u,0x1000u,0x2u})
  assert(!Check(klass,klass,"number","I",7,7,0x10000041|extra,0x41,&error)&&error.find("native Java modifiers")!=std::string::npos);
 assert(!Check(klass,klass,"number","I",7,7,1,0x41,&error));
 std::cout<<"Cache Field witness: runtime hidden-API flags preserve exact public/DEX modifiers; Class, name, type, FieldId and each language flag mismatch reject with granular diagnostics\n";
}
