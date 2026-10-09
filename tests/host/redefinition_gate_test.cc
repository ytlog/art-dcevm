#include "platform/common/redefinition_gate.cc"
#include <cassert>
#include <cstring>
#include <thread>
#include <condition_variable>
#include <iostream>
using namespace artdcevm;
namespace {
jvmtiPhase phase=JVMTI_PHASE_LIVE;bool phase_change=false;
jvmtiError JNICALL Phase(jvmtiEnv*,jvmtiPhase* out){*out=phase;return JVMTI_ERROR_NONE;}
int calls=0,rollbacks=0;bool nested=false,block=false;std::atomic<bool> entered{false},release_body{false};
void* expected[5]{};int32_t expected_type=0;
jvmtiError Body(void* env,void* runtime,void* self,const void* defs,int32_t type,void* error){
 assert(env==expected[0]&&runtime==expected[1]&&self==expected[2]&&defs==expected[3]&&error==expected[4]&&type==expected_type);
 ++calls;
 if(nested)assert(redefinition_gate::Proxy(env,runtime,self,defs,type,error)==JVMTI_ERROR_NOT_AVAILABLE);
 if(block){entered=true;while(!release_body.load())std::this_thread::yield();}
 return JVMTI_ERROR_FAILS_VERIFICATION;
}
struct Hooks:HookEngine {
 bool missing=false;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{assert(std::strstr(name,"RedefineClassesDirect"));return missing?nullptr:reinterpret_cast<void*>(Body);}
 void* Install(void*,void*,void** original,std::string*)override{*original=reinterpret_cast<void*>(Body);if(phase_change)phase=JVMTI_PHASE_LIVE;return this;}
 bool Remove(void*,std::string*)override{++rollbacks;return true;}
};
}
int main(){
 using namespace redefinition_gate;
 jvmtiInterface_1 functions{};functions.GetPhase=Phase;jvmtiEnv ti{&functions};JavaVM vm{},other{};Hooks hooks;std::string error;
 assert(!InstallStartup(hooks,&vm,&ti,&error)&&!Ready(&vm)&&error.find("preexisting")!=std::string::npos);
 phase=JVMTI_PHASE_ONLOAD;hooks.missing=true;assert(!InstallStartup(hooks,&vm,&ti,&error)&&!Ready(&vm));hooks.missing=false;
 phase_change=true;assert(!InstallStartup(hooks,&vm,&ti,&error)&&rollbacks==0&&!Ready(&vm));phase_change=false;phase=JVMTI_PHASE_ONLOAD;
 assert(InstallStartup(hooks,&vm,&ti,&error)&&Ready(&vm)&&!Ready(&other));
 int native[5];for(int n=0;n<5;++n)expected[n]=&native[n];expected_type=1;
 auto invoke=[&]{return Proxy(expected[0],expected[1],expected[2],expected[3],expected_type,expected[4]);};
 nested=true;assert(invoke()==JVMTI_ERROR_FAILS_VERIFICATION&&calls==1);nested=false;
 assert(!Owned(&vm));
 {Lease wrong(&other);assert(!wrong.Acquired()&&!Owned(&other));}
 {Lease lease(&vm);assert(lease.Acquired()&&Owned(&vm)&&!Owned(&other));Lease recursive(&vm);assert(!recursive.Acquired());
  std::thread denied([&]{assert(!Owned(&vm));assert(invoke()==JVMTI_ERROR_NOT_AVAILABLE);Lease other_thread(&vm);assert(!other_thread.Acquired());});denied.join();
  assert(calls==1);assert(invoke()==JVMTI_ERROR_FAILS_VERIFICATION&&calls==2);}
 assert(!Owned(&vm));
 block=true;std::thread active([&]{assert(invoke()==JVMTI_ERROR_FAILS_VERIFICATION);});while(!entered.load())std::this_thread::yield();
 {Lease denied(&vm);assert(!denied.Acquired());assert(invoke()==JVMTI_ERROR_NOT_AVAILABLE);}
 release_body=true;active.join();block=false;
 {Lease next(&vm);assert(next.Acquired());}
 std::cout<<"Actual Direct gate: startup proof, LIVE refusal, retained phase-change gate, all six arguments/results, nonblocking external serialization and recursion rejection PASS\n";
}
