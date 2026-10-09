#include "platform/common/redefinition_gate.cc"
#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
using namespace artdcevm;
namespace {
void* handler=nullptr;int initialized=0,calls=0,installs=0;bool race=false,missing=false,missing_direct=false,failed=false;
jvmtiError Body(void*,void*,void*,const void*,int32_t,void*){++calls;return JVMTI_ERROR_FAILS_VERIFICATION;}
struct Hooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* n)override{
  if(std::strstr(n,"gEventHandler"))return missing?nullptr:&handler;
  if(std::strstr(n,"RedefineClassesDirect"))return missing_direct?nullptr:reinterpret_cast<void*>(Body);
  return nullptr;
 }
 void* Install(void* target,void* replacement,void** saved,std::string*)override{
  assert(target==reinterpret_cast<void*>(Body));++installs;
  if(failed)return nullptr;
  // Immediate concurrent callers cannot dereference an unpublished original.
  assert(reinterpret_cast<redefinition_gate::Direct>(replacement)(nullptr,nullptr,nullptr,nullptr,0,nullptr)==JVMTI_ERROR_NOT_AVAILABLE);
  *saved=target;if(race)handler=&initialized;return target;
 }
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
void RunCase(int mode){
 if(mode==1)handler=&initialized; // initialized and all Envs since disposed
 if(mode==2)race=true; // initialization wins before post-install lifetime read
 if(mode==3)missing=true;
 if(mode==4)missing_direct=true;
 if(mode==5)failed=true;
 JavaVM vm{};Hooks hooks;std::string error;
 bool ok=redefinition_gate::InstallFirstLive(hooks,&vm,&error);
 assert(ok==(mode==0)&&redefinition_gate::Ready(&vm)==ok);
 if(ok){
  // The plugin may initialize and create any future Env after our proof.
  handler=&initialized;
  assert(redefinition_gate::InstallFirstLive(hooks,&vm,&error)&&installs==1);
  redefinition_gate::Lease lease(&vm);assert(lease.Acquired());
  assert(redefinition_gate::Proxy(nullptr,nullptr,nullptr,nullptr,0,nullptr)==JVMTI_ERROR_FAILS_VERIFICATION&&calls==1);
 }else{
  assert(!error.empty()||failed);
  assert(!redefinition_gate::Lease(&vm).Acquired());
 }
}
}
int main(){for(int mode=0;mode<6;++mode){auto child=fork();assert(child>=0);if(!child){RunCase(mode);_exit(0);}int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);}std::cout<<"Actual pristine-plugin gate: post-install non-reset lifetime witness, initialized/disposed history, initialization race, missing symbols, unavailable unpublished original, preserved source result PASS\n";}
