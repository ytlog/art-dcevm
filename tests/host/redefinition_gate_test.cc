#include "platform/common/redefinition_gate.cc"
#include <cassert>
#include <cstring>
#include <thread>
#include <condition_variable>
#include <iostream>
#include <stdexcept>
using namespace artdcevm;
namespace {
jvmtiPhase phase=JVMTI_PHASE_LIVE;bool phase_change=false;
jvmtiError JNICALL Phase(jvmtiEnv*,jvmtiPhase* out){*out=phase;return JVMTI_ERROR_NONE;}
std::atomic<int> calls{0};int rollbacks=0;bool nested=false,block=false,throw_body=false;
jvmtiError body_result=JVMTI_ERROR_FAILS_VERIFICATION;std::atomic<bool> entered{false},release_body{false};
thread_local void* pending_exception=nullptr;
void* expected[5]{};int32_t expected_type=0;
jvmtiError Body(void* env,void* runtime,void* self,const void* defs,int32_t type,void* error){
 assert(env==expected[0]&&runtime==expected[1]&&self==expected[2]&&defs==expected[3]&&error==expected[4]&&type==expected_type);
 ++calls;
 if(throw_body)throw std::runtime_error("original failure");
 if(nested){pending_exception=reinterpret_cast<void*>(0x1234);
  assert(redefinition_gate::Proxy(env,runtime,self,defs,type,error)==JVMTI_ERROR_INTERNAL);
  assert(pending_exception==reinterpret_cast<void*>(0x1234));
 }
 if(block){entered=true;while(!release_body.load())std::this_thread::yield();}
 return body_result;
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
 native[4]=0x5678;
 // The published startup gate remains transparent even before authority is
 // activated; both lifecycle paths share the same contention dispatcher.
 ready=false;nested=true;assert(invoke()==JVMTI_ERROR_FAILS_VERIFICATION&&calls==1);
 assert(native[4]==0x5678&&pending_exception==reinterpret_cast<void*>(0x1234));
 ready=true;nested=false;
 assert(!Owned(&vm));
 {Lease wrong(&other);assert(!wrong.Acquired()&&!Owned(&other));}
 {Lease lease(&vm);assert(lease.Acquired()&&Owned(&vm)&&!Owned(&other));Lease recursive(&vm);assert(!recursive.Acquired());
  std::thread denied([&]{assert(!Owned(&vm));assert(invoke()==JVMTI_ERROR_INTERNAL);Lease other_thread(&vm);assert(!other_thread.Acquired());});denied.join();
  assert(calls==1);assert(invoke()==JVMTI_ERROR_FAILS_VERIFICATION&&calls==2);}
 assert(!Owned(&vm));
 // Independent public agents: one commits, contention has no body side
 // effects, then the rejected caller retries successfully after release.
 body_result=JVMTI_ERROR_NONE;const auto before_contention=calls.load();
 block=true;std::thread active([&]{assert(invoke()==JVMTI_ERROR_NONE);});while(!entered.load())std::this_thread::yield();
 {Lease denied(&vm);assert(!denied.Acquired());assert(invoke()==JVMTI_ERROR_INTERNAL);}
 assert(calls==before_contention+1&&native[4]==0x5678);
 release_body=true;active.join();block=false;
 assert(invoke()==JVMTI_ERROR_NONE&&calls==before_contention+2);
 // Original exception unwinds only the current Direct interval; a private
 // lease remains owned until its outer RAII scope exits. A new public caller
 // can retry after ordinary failures and C++ exception propagation.
 throw_body=true;try{invoke();assert(false);}catch(const std::runtime_error&){}
 assert(!direct&&!Owned(&vm));throw_body=false;
 assert(invoke()==JVMTI_ERROR_NONE);
 {Lease lease(&vm);assert(lease.Acquired());throw_body=true;
  try{invoke();assert(false);}catch(const std::runtime_error&){}
  assert(!direct&&Owned(&vm));throw_body=false;
  std::thread denied([&]{assert(invoke()==JVMTI_ERROR_INTERNAL);Lease other_thread(&vm);assert(!other_thread.Acquired());});denied.join();
  assert(invoke()==JVMTI_ERROR_NONE);
 }
 assert(!Owned(&vm)&&invoke()==JVMTI_ERROR_NONE);
 {Lease next(&vm);assert(next.Acquired());}
 std::cout<<"Actual Direct gate: startup proof, LIVE refusal, retained phase-change gate, all six arguments/results, nonblocking public INTERNAL contention/retry, private exclusion, recursion and original exception rollback PASS\n";
}
