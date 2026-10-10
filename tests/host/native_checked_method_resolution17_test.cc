#include "platform/abi/arm64/android17/checked_method_resolution.h"
#include <cassert>
#include <iostream>
namespace a=artdcevm::abi::arm64::android17;
int main(){
 a::ResolvedDispatch m{0x1000,0,1};uintptr_t out=0xdead;
 auto good=[&](a::MethodDispatch kind,uintptr_t carrier,uintptr_t expected){out=0xdead;assert(a::DecodeCheckedMethod(kind,carrier,m,&out)&&out==expected);};
 auto bad=[&](a::MethodDispatch kind,uintptr_t carrier){out=0xdead;assert(!a::DecodeCheckedMethod(kind,carrier,m,&out)&&out==0xdead);};
 good(a::MethodDispatch::Virtual,0,0x1000); // Source index zero is success.
 bad(a::MethodDispatch::Virtual,0x1000);m.method_index=37;good(a::MethodDispatch::Virtual,37,0x1000);bad(a::MethodDispatch::Virtual,38);
 good(a::MethodDispatch::Super,0x2000,0x2000);bad(a::MethodDispatch::Super,0);bad(a::MethodDispatch::Super,0x2002);
 good(a::MethodDispatch::Direct,0x1000,0x1000);good(a::MethodDispatch::Static,0x1000,0x1000);bad(a::MethodDispatch::Direct,0x1001);bad(a::MethodDispatch::Static,0x1002);
 m.flags=0x401;good(a::MethodDispatch::Interface,0x1000,0x1000);bad(a::MethodDispatch::Interface,0x1002);m.flags=1;good(a::MethodDispatch::Interface,0x1002,0x1000);bad(a::MethodDispatch::Interface,0x1000);
 m.object_class=true;good(a::MethodDispatch::Interface,(37u<<16)|1,0x1000);bad(a::MethodDispatch::Interface,(38u<<16)|1);bad(a::MethodDispatch::Interface,0x1002);m.object_class=false;
 m.string_class=true;m.flags=0x10001;good(a::MethodDispatch::Direct,0x3001,0x1000);bad(a::MethodDispatch::Direct,0);bad(a::MethodDispatch::Direct,1);bad(a::MethodDispatch::Direct,0x3003);m.flags|=8;bad(a::MethodDispatch::Direct,0x3001);
 m.method=0;bad(a::MethodDispatch::Virtual,37);m.method=0x1001;bad(a::MethodDispatch::Virtual,37);m.method=0x1000;bad(static_cast<a::MethodDispatch>(9),0x1000);
 std::cout<<"Fixed Android17 original checked carrier: virtual zero/index, selected super, direct/static, interface Object/default/abstract and String constructor factory; wrong tag/type/index preserves output PASS\n";
}
