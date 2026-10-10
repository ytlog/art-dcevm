#include "platform/abi/arm64/android17/checked_method_resolution.h"
#include "platform/common/native_receiver_checked_caller.h"
#include <cassert>
#include <thread>
#include <iostream>
namespace a=artdcevm::abi::arm64::android17;
namespace {
using artdcevm::receiver_guard::CheckedCallerAccess;
using artdcevm::receiver_guard::EnsureCheckedCaller;
using artdcevm::method_metadata::Method12;
struct Fixture {Method12 old_method{},new_method{};int old_class=1,new_class=2,thread=3,foreign=4;unsigned clear_calls=0,roots=0;bool short_extent=false,no_clear=false,damage=false,change_root=false,change_before=false,change_after=false,pending_exception=true;};
thread_local Fixture* active=nullptr;thread_local void* actual=nullptr;
bool Current(void*,void*self)noexcept{return actual==self;}
bool Extent(void*raw,void*,const void*,size_t*out)noexcept{auto&f=*static_cast<Fixture*>(raw);*out=f.short_extent?31:32;if(f.change_before)actual=&f.foreign;return true;}
void* Root(void*raw,void*,void*method)noexcept{auto&f=*static_cast<Fixture*>(raw);++f.roots;if(f.change_root&&f.roots>1)return &f.foreign;if(method==&f.old_method)return &f.old_class;if(method==&f.new_method)return &f.new_class;return nullptr;}
void Clear(void*klass,a::MethodPointerSize size){auto&f=*active;assert(size==a::MethodPointerSize::k64);++f.clear_calls;assert(klass==&f.old_class||klass==&f.new_class);if(!f.no_clear)(klass==&f.old_class?f.old_method:f.new_method).access_flags&=~a::kSkipAccessChecks;if(f.damage)f.old_method.dex_method_index++;if(f.change_after)actual=&f.foreign;}
void Reset(Fixture&f){f=Fixture{};f.old_method.access_flags=f.new_method.access_flags=1|a::kSkipAccessChecks;f.old_method.dex_method_index=7;f.new_method.dex_method_index=9;active=&f;actual=&f.thread;}
CheckedCallerAccess Access(Fixture&f){return {&f,Current,Extent,Root,Clear};}
void Cases(){Fixture f;Reset(f);auto access=Access(f);
 assert(EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==1&&f.pending_exception);
 assert(EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==1);
 assert(EnsureCheckedCaller(access,&f.thread,&f.new_method)&&f.clear_calls==2);
 Reset(f);assert(!EnsureCheckedCaller(access,&f.foreign,&f.old_method)&&f.clear_calls==0);
 Reset(f);f.short_extent=true;assert(!EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==0);
 Reset(f);f.old_method.access_flags|=0x100;assert(!EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==0);
 Reset(f);f.old_method.access_flags|=0x400;assert(!EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==0);
 Reset(f);f.old_method.dex_method_index=UINT32_MAX;assert(!EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==0);
 Reset(f);f.no_clear=true;assert(!EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==1&&f.pending_exception);
 Reset(f);f.damage=true;assert(!EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==1);
 Reset(f);f.change_root=true;assert(!EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==1);
 Reset(f);f.change_before=true;assert(!EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==0);
 Reset(f);f.change_after=true;assert(!EnsureCheckedCaller(access,&f.thread,&f.old_method)&&f.clear_calls==1&&f.pending_exception);
 Reset(f);Method12 foreign{};foreign.access_flags=1|a::kSkipAccessChecks;foreign.dex_method_index=5;assert(!EnsureCheckedCaller(access,&f.thread,&foreign)&&f.clear_calls==0);
}
}
int main(){
 Cases();std::thread left(Cases),right(Cases);left.join();right.join();
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
 std::cout<<"Actual checked caller preparation (native leaf doubles): conditional original clear, exact Thread/root/member readback, two threads, old/new owners, pending preservation and wrong-context refusals PASS. Fixed Android17 original checked carrier: virtual zero/index, selected super, direct/static, interface Object/default/abstract and String constructor factory; wrong tag/type/index preserves output PASS\n";
}
