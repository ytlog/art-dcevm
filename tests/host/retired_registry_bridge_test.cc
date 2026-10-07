#include "platform/android13/retired_method_registry_bridge.h"
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/android13/retired_field_registry_abi.h"
#include "platform/android13/retired_member_guard.h"
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
namespace g=artdcevm::android13;
namespace {
void(*retire_method)(void*);bool(*contains_method)(void*);
g::RetiredFieldCallbacks fields{};
bool Contains(const void* allocator,void* member){return allocator==member;}
void* Current(){return nullptr;}
void Throw(void*,const char*,const char*){}
void Deliver(void*){}
class Hooks:public artdcevm::HookEngine { public:
 bool available=true;bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  if(!available)return nullptr;
  if(std::strstr(name,"ContainsUnsafe"))return reinterpret_cast<void*>(Contains);
  if(std::strstr(name,"CurrentFromGdb"))return reinterpret_cast<void*>(Current);
  if(std::strstr(name,"ThrowNewException"))return reinterpret_cast<void*>(Throw);
  if(std::strstr(name,"QuickDeliverException"))return reinterpret_cast<void*>(Deliver);
  return nullptr;
 }
 void* Install(void*,void*,void**,std::string*)override{return nullptr;}
 bool Remove(void*,std::string*)override{return true;}
};
}
namespace artdcevm::android13 {
bool ConfigureRetiredMemberGuard(const RetiredMemberGuardFunctions& f){return f.current_thread&&f.throw_exception&&f.deliver_exception&&f.contains_method&&f.contains_field;}
}
extern "C" void ArtDcevmSourceConfigureRetiredMethods(void(*retire)(void*),bool(*contains)(void*)){
 retire_method=retire;contains_method=contains;
}
extern "C" void ArtDcevmSourceConfigureRetiredFields(const g::RetiredFieldCallbacks* callbacks){fields=*callbacks;}
int main(){
 static_assert(sizeof(g::RetiredFieldCallbacks)==2*sizeof(void*));
 Hooks hooks;std::string error;auto* source=dlopen(nullptr,RTLD_NOW);assert(source);
 hooks.available=false;
 assert(!g::ConfigureRetiredMethodRegistry(hooks,source,&error));
 assert(!g::ConfigureRetiredFieldRegistry(hooks,source,&error));
 hooks.available=true;
 assert(g::ConfigureRetiredFieldRegistry(hooks,source,&error));
 assert(g::ConfigureRetiredMethodRegistry(hooks,source,&error));
 int method,field;assert(g::RetiredMethodCount()==0);
 retire_method(&method);retire_method(&method);
 assert(contains_method(&method)&&g::RetiredMethodCount()==1);
 fields.retire(&field);auto token=fields.token_for(&field);
 assert(token&g::kRetiredFieldTag);assert(g::ResolveRetiredFieldToken(token)==&field);
 assert(g::PruneRetiredMethods(&method)==1&&!contains_method(&method)&&g::RetiredMethodCount()==0);
 assert(g::PruneRetiredFields(&field)==1&&fields.token_for(&field)==0&&g::ResolveRetiredFieldToken(token)==nullptr);
 std::cout<<"retired registry bridge: minimal two-callback ABIs, canonical owned state, token identity and allocator prune passed\n";
}
