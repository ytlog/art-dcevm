#include "platform/android13/owned_jit_receiver_probe.h"
#include "platform/common/heap_collect_observer.h"
#include <jvmti.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
using namespace artdcevm;
using namespace artdcevm::android13;
namespace a=owned_jit_probe_abi;
namespace {
const char* mode="success";
a::Compile compile_hook; a::Collect collect_hook; a::Sweep sweep_hook;
void* jit=reinterpret_cast<void*>(0x1000);
void* cache=reinterpret_cast<void*>(0x2000);
void* heap=reinterpret_cast<void*>(0x3000);
bool pending=false;
int suspended=0,stops=0,starts=0,collections=0,sweeps=0,disposals=0,heap_installs=0,shared_observed=0;
void SharedObservation(void* receiver,bool clear,a::GcCause cause){assert(receiver==heap&&!clear&&cause==4);++shared_observed;}
bool Compile(void* receiver,void*,void* self,jit_service::CompilationKind12 kind,bool prejit){assert(receiver==jit || receiver==reinterpret_cast<void*>(0x7770));assert(self==heap&&kind==jit_service::CompilationKind12::kOptimized&&!prejit);return false;}
void Sweep(void* receiver,void*){assert(receiver==cache || receiver==reinterpret_cast<void*>(0x7770));++sweeps;}
void Collect(void* receiver,bool clear,a::GcCause cause){
 assert(!clear&&static_cast<int>(cause)==4);
 assert(receiver==heap);++collections;
 if(!std::strcmp(mode,"bypassed"))return;
 if(!std::strcmp(mode,"background")){std::thread thread([]{sweep_hook(cache,nullptr);});thread.join();}
 else sweep_hook(cache,nullptr);
 if(!std::strcmp(mode,"conflict"))sweep_hook(reinterpret_cast<void*>(0x7770),nullptr);
}
void SuspendJit(a::SuspendJit* storage){
 assert(!suspended);storage->was_on=std::strcmp(mode,"jit-absent")!=0;
 if(storage->was_on)++stops;
}
void ResumeJit(a::SuspendJit* storage){if(storage->was_on)++starts;}
void SuspendAll(void*,const char*,bool){assert(!suspended);++suspended;}
void ResumeAll(void*){assert(suspended==1);--suspended;}
void* Decode(void* manager,jmethodID id){assert(manager==heap);return reinterpret_cast<void*>(id);}
class Hooks:public HookEngine{
 public:
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  if(!std::strcmp(name,a::kCompile))return !std::strcmp(mode,"missing")?nullptr:reinterpret_cast<void*>(Compile);
  if(std::strstr(name,"DecodeMethodId"))return reinterpret_cast<void*>(Decode);
  if(!std::strcmp(name,a::kSweep))return reinterpret_cast<void*>(Sweep);
  if(!std::strcmp(name,a::kCollect))return reinterpret_cast<void*>(Collect);
  if(!std::strcmp(name,a::kSuspend))return reinterpret_cast<void*>(SuspendJit);
  if(!std::strcmp(name,a::kResume))return reinterpret_cast<void*>(ResumeJit);
  if(std::strstr(name,"C2"))return reinterpret_cast<void*>(SuspendAll);
  if(std::strstr(name,"D2"))return reinterpret_cast<void*>(ResumeAll);
  assert(false);return nullptr;
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override{
  assert(suspended==1);*original=target;
  if(target==reinterpret_cast<void*>(Compile))compile_hook=reinterpret_cast<a::Compile>(proxy);
  else if(target==reinterpret_cast<void*>(Collect)){assert(++heap_installs==1);collect_hook=reinterpret_cast<a::Collect>(proxy);}
  else{assert(target==reinterpret_cast<void*>(Sweep));sweep_hook=reinterpret_cast<a::Sweep>(proxy);}
  return target;
 }
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
jvmtiInterface_1_ ti_table{};jvmtiEnv ti{&ti_table};
JNIInvokeInterface_ vm_table{};JavaVM vm{&vm_table};
jboolean JNICALL Exception(JNIEnv*){return pending?JNI_TRUE:JNI_FALSE;}
jint JNICALL GetVm(JNIEnv*,JavaVM** out){*out=&vm;return JNI_OK;}
jint JNICALL GetEnv(JavaVM*,void** out,jint version){assert(version==JVMTI_VERSION_1_2);*out=&ti;return !std::strcmp(mode,"env-error")?JNI_ERR:JNI_OK;}
jvmtiError JNICALL ForceGc(jvmtiEnv* owned){
 assert(owned==&ti && starts==stops);
 collect_hook(heap,false,static_cast<a::GcCause>(4));
 return !std::strcmp(mode,"collection-error")?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Dispose(jvmtiEnv* owned){assert(owned==&ti);++disposals;return !std::strcmp(mode,"cleanup-error")?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
}
jclass JNICALL Owner(JNIEnv*,jobject){return reinterpret_cast<jclass>(0x5000);}
jmethodID JNICALL Method(JNIEnv*,jclass,const char*,const char*){return reinterpret_cast<jmethodID>(0x4000);}
void JNICALL Delete(JNIEnv*,jobject){}
jint JNICALL Call(JNIEnv*,jobject,jmethodID id,va_list){
 if(!std::strcmp(mode,"jit-absent"))return 38;
 if(!std::strcmp(mode,"java-error"))return 0;
 if(!std::strcmp(mode,"java-exception")){pending=true;return 0;}
 std::thread worker([&]{
  void* method=!std::strcmp(mode,"foreign-method")?reinterpret_cast<void*>(0x9990):reinterpret_cast<void*>(id);
  assert(!compile_hook(jit,method,heap,jit_service::CompilationKind12::kOptimized,false));
  if(!std::strcmp(mode,"jit-conflict"))compile_hook(reinterpret_cast<void*>(0x7770),method,heap,jit_service::CompilationKind12::kOptimized,false);
 });worker.join();
 return 38;
}
int main(int argc,char** argv){
 if(argc>1)mode=argv[1];
 ti_table.ForceGarbageCollection=ForceGc;ti_table.DisposeEnvironment=Dispose;vm_table.GetEnv=GetEnv;
 JNINativeInterface_ table{};table.ExceptionCheck=Exception;table.GetJavaVM=GetVm;table.GetObjectClass=Owner;table.GetMethodID=Method;table.DeleteLocalRef=Delete;table.CallIntMethodV=Call;JNIEnv env{&table};
 Hooks hooks;OwnedJitReceivers result{reinterpret_cast<void*>(0xaa),nullptr,nullptr};std::string error;
 assert(heap_collect_observer::Install(hooks,SharedObservation,&error));
 assert(heap_collect_observer::Install(hooks,SharedObservation,&error));
 pending=!std::strcmp(mode,"pending-exception");
 bool ok=CalibrateOwnedJitReceivers(hooks,&env,reinterpret_cast<jobject>(0x6000),heap,&result,&error);
 if(!std::strcmp(mode,"success")){
  assert(ok && error.empty() && result.jit==jit && result.code_cache==cache && result.heap==heap);
  assert(CalibrateOwnedJitReceivers(hooks,&env,reinterpret_cast<jobject>(0x6000),heap,&result,&error));
  assert(stops==2 && starts==2 && collections==2 && sweeps==2 && disposals==2 && heap_installs==1 && shared_observed==2);
  compile_hook(jit,nullptr,heap,jit_service::CompilationKind12::kOptimized,false);sweep_hook(cache,nullptr); // Unrelated work is passthrough.
 }else{
  assert(!ok && !error.empty() && result.jit==reinterpret_cast<void*>(0xaa) && !result.code_cache && !result.heap);
  if(std::strcmp(mode,"missing") && std::strcmp(mode,"java-error") && std::strcmp(mode,"java-exception") && std::strcmp(mode,"pending-exception") && std::strcmp(mode,"env-error"))assert(disposals==1 && starts==stops);
  else assert(disposals==0);
 }
 assert(!suspended);
 std::cout<<"Owned JIT receiver probe "<<mode<<": passed\n";
}
