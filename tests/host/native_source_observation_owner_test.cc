#include "platform/common/native_source_observation_owner.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace {
alignas(8) unsigned char thread[512]{};void* actual_thread=thread;
JavaVM vm{};void* actual_vm=&vm;int suspended=0,stops=0,resumes=0,binds=0,adds=0,finishes=0;
bool add_success=true,finish_success=true,lose_owner=false;
void* Current(){return actual_thread;}
void Stop(void*,const char*,bool){assert(!suspended);suspended=1;++stops;if(lose_owner)actual_thread=nullptr;}
void Resume(void*){assert(suspended);suspended=0;++resumes;if(lose_owner)actual_thread=thread;}
jint JNICALL GetVm(JNIEnv*,JavaVM** out){assert(!suspended);*out=static_cast<JavaVM*>(actual_vm);return JNI_OK;}
void State(uint32_t value){uint32_t flags=value<<abi::arm64::android11::kStateShift;std::memcpy(thread+abi::arm64::android11::kThreadFlagsOffset,&flags,4);}
struct Hooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* symbol)override{
  if(std::strcmp(symbol,abi::arm64::android11::kCurrentThread)==0)return reinterpret_cast<void*>(Current);
  if(std::strcmp(symbol,abi::common::kSuspendConstruct)==0)return reinterpret_cast<void*>(Stop);
  if(std::strcmp(symbol,abi::common::kSuspendDestroy)==0)return reinterpret_cast<void*>(Resume);
  return nullptr;
 }
 void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
}
namespace artdcevm::native_source_class_loading {
bool Service::Bind(HookEngine&,const runtime_services::Snapshot&,Service*,std::string*){assert(!suspended);++binds;return true;}
bool Install(HookEngine&,Service&,std::string*){assert(suspended);++adds;return add_success;}
}
namespace artdcevm::native_hierarchy_session {
bool InstallPauseObservation(HookEngine&,std::string*){assert(suspended);++finishes;return finish_success;}
}
int main(){
 Hooks hooks;JNINativeInterface_ table{};table.GetJavaVM=GetVm;JNIEnv env{&table};
 runtime_services::Snapshot runtime;int runtime_storage;runtime.runtime=&runtime_storage;runtime.receivers.java_vm=&vm;
 thread_suspend_service::Service state;
 assert(thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kLegacy16,[&](const char*s){return hooks.FindSymbol("libart.so",s);},&state));
 native_source_observation::Owner owner;std::string error;
 State(abi::arm64::android11::kRunnable);
 assert(!owner.Install(hooks,&env,runtime,state,thread,sizeof(thread),&error)&&stops==0&&binds==0);
 State(abi::arm64::android11::kNative);
 actual_thread=nullptr;
 assert(!owner.Install(hooks,&env,runtime,state,thread,sizeof(thread),&error)&&stops==0);
 actual_thread=thread;actual_vm=nullptr;
 assert(!owner.Install(hooks,&env,runtime,state,thread,sizeof(thread),&error)&&stops==0);
 actual_vm=&vm;runtime.receivers.java_vm=nullptr;
 assert(!owner.Install(hooks,&env,runtime,state,thread,sizeof(thread),&error)&&stops==0);
 runtime.receivers.java_vm=&vm;lose_owner=true;
 assert(!owner.Install(hooks,&env,runtime,state,thread,sizeof(thread),&error));
 assert(!suspended&&stops==1&&resumes==1&&binds==1&&adds==0&&finishes==0);
 lose_owner=false;finish_success=false;
 assert(!owner.Install(hooks,&env,runtime,state,thread,sizeof(thread),&error));
 assert(!suspended&&stops==2&&resumes==2&&binds==1&&adds==1&&finishes==1);
 finish_success=true;
 assert(owner.Install(hooks,&env,runtime,state,thread,sizeof(thread),&error));
 assert(!suspended&&stops==3&&resumes==3&&binds==1&&adds==2&&finishes==2);
 assert(owner.Install(hooks,&env,runtime,state,thread,sizeof(thread),&error)&&stops==3);
 int different_runtime;runtime.runtime=&different_runtime;
 assert(!owner.Install(hooks,&env,runtime,state,thread,sizeof(thread),&error)&&stops==3);
 runtime.runtime=&runtime_storage;
 thread_suspend_service::Service different_state;
 assert(thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kModern24,[&](const char*s){return hooks.FindSymbol("libart.so",s);},&different_state));
 uint32_t modern_flags=abi::arm64::android11::kNative<<abi::arm64::android13::kStateShift;
 std::memcpy(thread+abi::arm64::android13::kThreadFlagsOffset,&modern_flags,4);
 assert(!owner.Install(hooks,&env,runtime,different_state,thread,sizeof(thread),&error)&&stops==3);
 State(abi::arm64::android11::kRunnable);
 assert(!owner.Install(hooks,&env,runtime,state,thread,sizeof(thread),&error)&&stops==3);
 std::cout<<"Native source observer installation: exact VM/self/state, actual stop release, partial-install retry and immutable owner PASS\n";
}
