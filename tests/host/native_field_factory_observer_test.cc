#include "platform/common/native_field_factory_observer.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;namespace obs=native_field_factory_observer;
static int thread,other,root_owner,field1,field2,klass;static void*current=&thread;static bool pending=false,throw_reader=false;static unsigned original_calls=0,root_calls=0;static obs::Factory proxy=nullptr;using Three=obs::Return(*)(void*,void*,bool);static Three proxy3=nullptr;static bool normalize_fail=false;static void*normalize_input=nullptr;
static void* Current(){return current;}
static obs::Return Original(void*self,obs::HandleCarrier handle,void*field,bool force){assert(self==current&&handle.slot&&field);++original_calls;if(force){pending=true;return {0};}return {uintptr_t(field)};}
static bool Root(void*owner,void*self,const void*slot,void**out){assert(owner==&root_owner&&self==current&&!pending);++root_calls;if(throw_reader)throw 7;*out=*static_cast<void*const*>(slot);return true;}
static obs::Return OriginalThree(void*self,void*field,bool force){assert(self==current&&field);++original_calls;if(force){pending=true;return {0};}return {uintptr_t(field)};}
static bool Normalize(void*owner,void*self,obs::HandleCarrier,void*input,bool,void**out)noexcept{assert(owner==&root_owner&&self==current);normalize_input=input;if(normalize_fail){pending=true;return false;}*out=input==&field1?&field2:input;return true;}
class Hooks:public HookEngine {public:int family=4;bool deny=false,fail_remove=false;unsigned installs=0,removes=0;bool Initialize(std::string*)override{return true;}
 void*FindSymbol(const char*l,const char*n)override{assert(!std::strcmp(l,"libart.so"));
 if(!std::strcmp(n,"_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadENS_6HandleINS0_5ClassEEEPNS_8ArtFieldEb"))return family==4?reinterpret_cast<void*>(Original):nullptr;
 if(!std::strcmp(n,"_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadEPNS_8ArtFieldEb"))return family==3?reinterpret_cast<void*>(OriginalThree):nullptr;
 assert(!std::strcmp(n,"_ZN3art6mirror5Field18CreateFromArtFieldILNS_11PointerSizeE8ELb0EEENS_6ObjPtrIS1_EEPNS_6ThreadEPNS_8ArtFieldEb"));return family==11?reinterpret_cast<void*>(OriginalThree):nullptr;}
 void*Install(void*t,void*p,void**out,std::string*)override{assert(t==(family==4?reinterpret_cast<void*>(Original):reinterpret_cast<void*>(OriginalThree)));++installs;if(deny)return nullptr;if(family==4)proxy=reinterpret_cast<obs::Factory>(p);else proxy3=reinterpret_cast<Three>(p);*out=t;return this;}
 bool Remove(void*token,std::string*)override{assert(token==this);++removes;if(fail_remove)return false;proxy=nullptr;proxy3=nullptr;return true;}
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
 for(int family:{4,3,11}){Hooks shared;shared.family=family;obs::Service sole;assert(sole.Bind(shared,bindings,&error));assert(sole.FourArgument()==(family==4));
 assert(!sole.NormalizerInstalled());assert(!sole.SetNormalizerAtExclusive({&root_owner,Normalize},wrong,&error));assert(sole.SetNormalizerAtExclusive({&root_owner,Normalize},context,&error));assert(!sole.SetNormalizerAtExclusive({&root_owner,Normalize},context,&error));
 assert(sole.InstallAtExclusive(shared,context,&error)&&sole.NormalizerInstalled());assert(sole.Begin(&thread,{&field1,&field2},&error));
 auto call=[&](void*f){return family==4?proxy(&thread,handle,f,false):proxy3(&thread,f,false);};
 assert(call(&field1).reference==uintptr_t(&field2)&&normalize_input==&field1);assert(call(&field2).reference==uintptr_t(&field2));assert(sole.End(&trace,&error));assert(trace.entries[0].field==&field1&&trace.entries[0].normalized_field==&field2&&trace.entries[0].normalization_invoked&&trace.entries[1].normalized_field==&field2);
 normalize_fail=true;const auto before=original_calls;assert(!call(&field1).reference&&pending&&original_calls==before);pending=false;normalize_fail=false;
 assert(sole.RemoveAtExclusive(shared,context,&error)&&!sole.NormalizerInstalled());}
 std::cout<<"Original typed Field observer: real exclusive callback boundary, unchanged arguments/return/pending, two caller native targets, unrelated Thread forwarding, bounded root diagnostics, no post-OOME reads, reader failure isolation and poisoned removal refusal and sole 3/4/template normalizer identity/alias/refusal PASS; no readiness permission\n";
}
