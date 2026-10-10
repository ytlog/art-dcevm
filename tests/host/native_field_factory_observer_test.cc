#include "platform/common/native_field_factory_observer.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;namespace obs=native_field_factory_observer;
static int thread,other,root_owner,field1,field2,klass;static void*current=&thread;static bool pending=false,throw_reader=false;static unsigned original_calls=0,root_calls=0;static obs::Factory proxy=nullptr;
static void* Current(){return current;}
static obs::Return Original(void*self,obs::HandleCarrier handle,void*field,bool force){assert(self==current&&handle.slot&&field);++original_calls;if(force){pending=true;return {0};}return {uintptr_t(field)};}
static bool Root(void*owner,void*self,const void*slot,void**out){assert(owner==&root_owner&&self==current&&!pending);++root_calls;if(throw_reader)throw 7;*out=*static_cast<void*const*>(slot);return true;}
class Hooks:public HookEngine {public:bool deny=false,fail_remove=false;unsigned installs=0,removes=0;bool Initialize(std::string*)override{return true;}
 void*FindSymbol(const char*l,const char*n)override{assert(!std::strcmp(l,"libart.so")&&!std::strcmp(n,"_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadENS_6HandleINS0_5ClassEEEPNS_8ArtFieldEb"));return reinterpret_cast<void*>(Original);}
 void*Install(void*t,void*p,void**out,std::string*)override{assert(t==reinterpret_cast<void*>(Original));++installs;if(deny)return nullptr;proxy=reinterpret_cast<obs::Factory>(p);*out=t;return this;}
 bool Remove(void*token,std::string*)override{assert(token==this);++removes;if(fail_remove)return false;proxy=nullptr;return true;}
};
int main(){Hooks hooks;obs::Bindings bindings{&root_owner,Current,Root};obs::Service observer;std::string error;
 assert(observer.Bind(hooks,bindings,&error));void* roots[]{&klass};exclusive_owned_roots::Context context{&thread,roots,1},wrong{&other,roots,1};
 assert(!observer.InstallAtExclusive(hooks,wrong,&error)&&!hooks.installs);assert(observer.InstallAtExclusive(hooks,context,&error));
 void* handle_root=&klass;obs::HandleCarrier handle{&handle_root};obs::Trace trace;
 assert(!observer.Begin(&thread,{&field1,&field1},&error));assert(observer.Begin(&thread,{&field1,&field2},&error));
 assert(proxy(&thread,handle,&field1,false).reference==uintptr_t(&field1));assert(proxy(&thread,handle,&field2,false).reference==uintptr_t(&field2));
 current=&other;assert(proxy(&other,handle,&field1,false).reference==uintptr_t(&field1));current=&thread;
 assert(!observer.RemoveAtExclusive(hooks,context,&error));assert(observer.End(&trace,&error));assert(trace.entries[0].calls==1&&trace.entries[1].calls==1&&trace.entries[0].false_calls==1&&trace.entries[0].root_before==uintptr_t(&klass)&&trace.entries[0].root_after==uintptr_t(&klass)&&original_calls==3&&root_calls==4);
 assert(observer.Begin(&thread,{&field1,&field2},&error));assert(proxy(&thread,handle,&field1,false).reference==uintptr_t(&field1));const auto reads=root_calls;assert(!proxy(&thread,handle,&field1,true).reference&&pending);assert(observer.End(&trace,&error));assert(trace.entries[0].calls==2&&trace.entries[0].false_calls==1&&!trace.entries[0].root_after_valid&&!trace.entries[0].root_after&&root_calls==reads+1);pending=false;
 throw_reader=true;assert(observer.Begin(&thread,{&field1,&field2},&error));assert(proxy(&thread,handle,&field1,false).reference==uintptr_t(&field1));assert(observer.End(&trace,&error)&&!trace.entries[0].root_before_valid&&!trace.entries[0].root_after_valid);throw_reader=false;
 assert(observer.RemoveAtExclusive(hooks,context,&error)&&hooks.removes==1);
 assert(observer.InstallAtExclusive(hooks,context,&error));hooks.fail_remove=true;assert(!observer.RemoveAtExclusive(hooks,context,&error)&&observer.Poisoned());assert(!observer.Begin(&thread,{&field1,&field2},&error));hooks.fail_remove=false;assert(observer.RemoveAtExclusive(hooks,context,&error));assert(!observer.InstallAtExclusive(hooks,context,&error));
 Hooks denied;denied.deny=true;obs::Service failure;assert(failure.Bind(denied,bindings,&error));assert(!failure.InstallAtExclusive(denied,context,&error)&&!failure.Poisoned());
 std::cout<<"Original typed Field observer: real exclusive callback boundary, unchanged arguments/return/pending, two caller native targets, unrelated Thread forwarding, bounded root diagnostics, no post-OOME reads, reader failure isolation and poisoned removal refusal PASS; no readiness permission\n";
}
