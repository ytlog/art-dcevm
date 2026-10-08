#include "platform/common/exclusive_owned_roots.h"
#include "platform/common/runnable_executor.h"
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <iostream>
using namespace artdcevm;
namespace {
alignas(16) uint32_t thread_word=92u<<24;
int stopped=0,starts=0,ends=0,stops=0,releases=0;
bool wrong_scope=false,pending=false,empty_when_stopped=false;
jobject local=nullptr;
void* Current(){return &thread_word;}
void Native(void* self){assert(self==Current()&&!stopped&&thread_word==0);++starts;thread_word=92u<<24;}
void Runnable(void* self){assert(self==Current()&&!stopped&&thread_word==(92u<<24));++ends;thread_word=0;}
void Stop(void*,const char*,bool){assert(!stopped&&thread_word==(92u<<24));++stops;++stopped;}
void Release(void*){assert(stopped==1&&thread_word==(92u<<24));++releases;--stopped;}
abi::arm64::android11::ReleaseObjectPointer Decode(const void* self,jobject global){assert(self==Current());return {empty_when_stopped&&stopped?0:reinterpret_cast<uintptr_t>(global)};}
jboolean Exception(JNIEnv*){assert(!stopped);return pending;}
jobjectRefType Kind(JNIEnv*,jobject value){assert(!stopped);return value==local?JNILocalRefType:JNIGlobalRefType;}
class Hooks:public HookEngine {
 public:
 bool missing=false;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override {
  if(missing)return nullptr;
  if(!std::strcmp(name,abi::arm64::android11::kCurrentThread))return reinterpret_cast<void*>(Current);
  if(!std::strcmp(name,"artJniMethodStart"))return reinterpret_cast<void*>(Native);
  if(!std::strcmp(name,"artJniMethodEnd"))return reinterpret_cast<void*>(Runnable);
  if(!std::strcmp(name,abi::arm64::android_common::kSuspendConstruct))return reinterpret_cast<void*>(Stop);
  if(!std::strcmp(name,abi::arm64::android_common::kSuspendDestroy))return reinterpret_cast<void*>(Release);
  assert(!std::strcmp(name,runnable_jni::GlobalDecoder::SymbolName()));return reinterpret_cast<void*>(Decode);
 }
 void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
bool Inspect(const exclusive_owned_roots::Context& scope,void* input){
 assert(stopped==1&&scope.self==Current()&&scope.count==2);
 auto* globals=static_cast<jobject*>(input);
 for(size_t i=0;i<2;++i)assert(scope.roots[i]==reinterpret_cast<void*>(globals[i]));
 return true;
}
bool Reject(const exclusive_owned_roots::Context&,void*){assert(stopped==1);return false;}
bool Throw(const exclusive_owned_roots::Context&,void*){assert(stopped==1);throw std::runtime_error("recovery");}
}
namespace artdcevm::runnable_executor {
runnable_jni::Result With(JNIEnv* env,jobject,runnable_jni::Operation operation,void* data){
 assert(thread_word==(92u<<24));thread_word=0;
 runnable_jni::Result result;
 try {result=operation({wrong_scope?nullptr:Current(),env,reinterpret_cast<JavaVM*>(1)},data)?runnable_jni::Result::kSuccess:runnable_jni::Result::kOperationRejected;}
 catch(...){result=runnable_jni::Result::kOperationThrew;}
 assert(thread_word==0&&!stopped);thread_word=92u<<24;return result;
}
}
int main(){
 JNINativeInterface_ table{};table.ExceptionCheck=Exception;table.GetObjectRefType=Kind;
 JNIEnv env{&table};Hooks hooks;exclusive_owned_roots::Service service;std::string error;
 assert(exclusive_owned_roots::Service::Bind(hooks,&service,&error));
 uint32_t objects[2]{};jobject globals[]={reinterpret_cast<jobject>(&objects[0]),reinterpret_cast<jobject>(&objects[1])};
 assert(service.With(&env,globals,2,Inspect,globals,&error));
 assert(!service.With(&env,globals,2,Reject,nullptr,&error));
 assert(!service.With(&env,globals,2,Throw,nullptr,&error));
 assert(service.With(&env,globals,2,Inspect,globals,&error));
 assert(starts==4&&ends==4&&stops==4&&releases==4&&!stopped);
 empty_when_stopped=true;assert(!service.With(&env,globals,2,Inspect,globals,&error));empty_when_stopped=false;
 wrong_scope=true;assert(!service.With(&env,globals,2,Inspect,globals,&error));wrong_scope=false;
 local=globals[1];assert(!service.With(&env,globals,2,Inspect,globals,&error));local=nullptr;
 pending=true;assert(!service.With(&env,globals,2,Inspect,globals,&error));pending=false;
 thread_word=92u<<16;assert(!service.With(&env,globals,2,Inspect,globals,&error));thread_word=92u<<24;
 hooks.missing=true;assert(!exclusive_owned_roots::Service::Bind(hooks,&service,&error));
 assert(service.With(&env,globals,2,Inspect,globals,&error));
 assert(starts==6&&ends==6&&stops==6&&releases==6);
 std::cout<<"Exclusive roots: stop/reload/order, false/throw recovery, Native/Runnable identity, invalid inputs and unchanged binding passed\n";
}
