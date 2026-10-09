#include "platform/common/jni_receiver_hook_lifetime.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <functional>
#include <sys/wait.h>
#include <unistd.h>
using Installer=artdcevm::jni_receiver_hook_lifetime::Installer<sizeof(void*)>;
using Scope=artdcevm::abi::common::RunnableJniScope;
static Installer installer;
static bool suspended=false,pin_ok=true,pinned=false,skip=false,exception=false;
static int installs=0,removes=0,resumes=0;
static std::atomic<int> originals{0};
static thread_local int self;
static std::function<void()> nested;
static JavaVM vm{};
static artdcevm::abi::common::FromReflectedMethod proxy=nullptr;
jobject carrier=reinterpret_cast<jobject>(0x2000);
void* method=reinterpret_cast<void*>(0x3000);
void* Current(){return &self;}
jboolean Exception(JNIEnv*){return exception;}
jint GetVM(JNIEnv*,JavaVM** out){assert(!suspended);if(nested && artdcevm::borrowed_jni_callback::Bridge::HasActiveInvocation())nested();*out=&vm;return JNI_OK;}
void Suspend(void*,const char*,bool){assert(!suspended&&pinned);suspended=true;}
void Resume(void*){assert(suspended);++resumes;suspended=false;}
void* Original(const Scope&,jobject object){assert(!suspended&&object==carrier);++originals;return method;}
jmethodID FromReflected(JNIEnv* env,jobject object){
 assert(!suspended);
 if(skip)return reinterpret_cast<jmethodID>(0x4000);
 Scope scope{&self,env,&vm};assert(proxy(scope,object)==method);
 return reinterpret_cast<jmethodID>(0x4000);
}
bool Pin(void*,std::string*){assert(!suspended);pinned=pin_ok;return pin_ok;}
struct Hooks:artdcevm::HookEngine {
 bool missing_suspend=false,install_fail=false,original_missing=false,remove_fail=false;
 bool Initialize(std::string*)override{assert(!suspended);return true;}
 void* FindSymbol(const char*,const char* name)override {
  using namespace artdcevm::abi::common;
  if(!std::strcmp(name,kCurrentThread))return reinterpret_cast<void*>(Current);
  if(!std::strcmp(name,kSuspendConstruct))return missing_suspend?nullptr:reinterpret_cast<void*>(Suspend);
  if(!std::strcmp(name,kSuspendDestroy))return reinterpret_cast<void*>(Resume);
  assert(!std::strcmp(name,kFromReflectedMethod));return reinterpret_cast<void*>(Original);
 }
 void* Install(void*,void* replacement,void** original,std::string*)override {
  assert(suspended && pinned && !installer.Bound());++installs;
  if(install_fail)return nullptr;
  proxy=reinterpret_cast<decltype(proxy)>(replacement);
  *original=original_missing?nullptr:reinterpret_cast<void*>(Original);
  return this;
 }
 bool Remove(void*,std::string*)override{assert(suspended&&!installer.Bound());++removes;proxy=nullptr;return !remove_fail;}
};
int main() {
 JNINativeInterface_ functions{};functions.ExceptionCheck=Exception;functions.GetJavaVM=GetVM;functions.FromReflectedMethod=FromReflected;
 struct Env {JNIEnv env;void* self;JavaVM* vm;} env{{&functions},&self,&vm};
 Hooks hooks;std::string error;
 auto run=[&]{return installer.InitializeFromNative(hooks,&env.env,carrier,sizeof(env),Pin,nullptr,&error);};
 hooks.missing_suspend=true;assert(!run()&&!installs&&!installer.Bound());hooks.missing_suspend=false;
 pin_ok=false;assert(!run()&&!installs&&!installer.Bound());pin_ok=true;
 env.self=nullptr;assert(!run()&&!installs);env.self=&self;
 exception=true;assert(!run()&&exception&&!installs);exception=false;
 hooks.install_fail=true;assert(!run()&&!installer.Bound()&&resumes==1&&!suspended);hooks.install_fail=false;
 hooks.original_missing=true;assert(!run()&&!installer.Bound()&&removes==1&&resumes==2&&!suspended);
 // Resuming with an unbound hook after failed rollback must never fabricate a
 // method pointer. This child verifies the fatal handoff boundary explicitly.
 pid_t child=fork();assert(child>=0);
 if(!child){hooks.remove_fail=true;run();_exit(99);}
 int status=0;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 hooks.original_missing=false;skip=true;
 assert(!run() && installer.Bound() && !installer.Ready() && !suspended);
 const int published_installs=installs,published_resumes=resumes;
 skip=false;int nested_hits=0;
 nested=[&]{++nested_hits;assert(!run());};
 assert(run()&&installer.Ready()&&!suspended&&nested_hits==1);
 nested={};
 assert(installs==published_installs && resumes==published_resumes); // retained, never reinstalled.
 assert(originals==1);assert(run()&&originals==2);
 Scope ordinary{&self,&env.env,&vm};assert(proxy(ordinary,carrier)==method&&originals==3);
 exception=true;assert(!run()&&exception&&installer.Ready()); // per-call refusal, existing proof retained.
 exception=false;
 std::vector<std::thread> workers;
 for(int i=0;i<16;++i)workers.emplace_back([&]{
   Env local{{&functions},&self,&vm};std::string local_error;
   assert(installer.InitializeFromNative(hooks,&local.env,carrier,sizeof(local),Pin,nullptr,&local_error));
   Scope context{&self,&local.env,&vm};assert(proxy(context,carrier)==method);
 });
 for(auto& worker:workers)worker.join();
 assert(originals==35 && installs==published_installs && resumes==published_resumes);
 std::cout<<"JNI hook lifetime: exclusive original publication, rollback/fatal boundary, real callback readiness and retained passthrough passed\n";
}
