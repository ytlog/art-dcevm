#include "platform/common/field_declaring_class_source.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace source=field_declaring_class_source;
static int owner_a,owner_b;static void* current_owner=&owner_a;
static bool wrong=false,cookie=false;static size_t original_calls=0,generic_calls=0;
static abi::arm64::android11::ReleaseObjectPointer Original(void* field){assert(field);++original_calls;return {cookie?(uintptr_t(current_owner)^UINT64_C(0x100000000)):uintptr_t(wrong?&owner_b:current_owner)};}
static void* Generic(void* field){assert(field);++generic_calls;return current_owner;}
class Hooks:public HookEngine {
 public:bool named=true,generic=true;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char* lib,const char* name)override{
  assert(!std::strcmp(lib,"libart.so"));
  if(!std::strcmp(name,source::Source::kOriginal))return named?reinterpret_cast<void*>(Original):nullptr;
  assert(!std::strcmp(name,"artReadBarrierForRootSlow"));return generic?reinterpret_cast<void*>(Generic):nullptr;
 }
 void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
int main(){
 abi::arm64::Field instance{0,0x20001,0,0},statik{0,0x40009,1,0};std::string error;
 for(bool named:{false,true}){Hooks hooks;hooks.named=named;source::Source s;assert(s.Bind(hooks,&error));assert(s.Named()==named&&!s.Ready()&&!s.Read(&instance));
  s.Begin();assert(s.Observe(&instance,current_owner,1,&error)&&s.Observe(&statik,current_owner,9,&error)&&s.Complete(&error));
  const auto original_before=original_calls,generic_before=generic_calls;assert(s.Read(&instance)==current_owner);
  assert(named?(original_calls==original_before+1&&generic_calls==generic_before):(generic_calls==generic_before+1&&original_calls==original_before));
  // A rooted owner's current identity changes independently; Read never caches
  // that old managed address or a previously successful return carrier.
  current_owner=&owner_b;assert(s.Read(&statik)==&owner_b);current_owner=&owner_a;
  s.Begin();assert(s.Observe(&instance,current_owner,1,&error));assert(!s.Complete(&error)&&!s.Ready());
  s.Begin();assert(s.Observe(&instance,current_owner,1,&error));assert(!s.Observe(&instance,current_owner,1,&error)&&!s.Complete(&error));
  s.Begin();assert(!s.Observe(&statik,current_owner,1,&error)&&!s.Complete(&error));
  s.Begin();assert(!s.Observe(&instance,nullptr,1,&error)&&!s.Complete(&error));
  s.Begin();assert(s.Observe(&instance,current_owner,1,&error));s.Reject();assert(!s.Complete(&error)&&!s.Read(&instance));
 }
 Hooks hooks;source::Source named;assert(named.Bind(hooks,&error));
 for(bool* refusal:{&wrong,&cookie}){*refusal=true;named.Begin();const auto before=generic_calls;
  assert(!named.Observe(&instance,current_owner,1,&error)&&error.find("ObjPtr codec differs")!=std::string::npos);
  assert(!named.Complete(&error)&&!named.Read(&instance)&&generic_calls==before);*refusal=false;
 }
 hooks.named=hooks.generic=false;source::Source absent;assert(!absent.Bind(hooks,&error)&&!absent.Ready());
 std::cout<<"Original Field declaring producer: exact public owner/ObjPtr codec/static+instance coverage, moved current root, high runtime flags, duplicate/foreign/cookie rejection and present-but-failed no generic fallback PASS\n";
}
