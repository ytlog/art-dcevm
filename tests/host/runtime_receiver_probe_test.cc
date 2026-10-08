#include "platform/android13/runtime_receiver_probe.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
using namespace artdcevm::android13;
using Carrier=abi::arm64::android11::ReleaseObjectPointer;
using Find=Carrier (*)(void*,void*,const char*,void*);
using Encode=jmethodID (*)(void*,void*);
namespace {
using SuspendThreads=void (*)(void*,const char*,bool);
SuspendThreads suspend_hook;
void RealSuspend(void* list,const char*,bool){assert(list==reinterpret_cast<void*>(0x8000));}
Find find_hook;Find real_delegate;int delegated=0;
Carrier Delegate(void* l,void* t,const char* d,void* h){++delegated;return real_delegate(l,t,d,h);}
Encode encode_hook;int suspended=0,finds=0,encodes=0;
const char* mode="success";
void* linker=reinterpret_cast<void*>(0x1000);
void* manager=reinterpret_cast<void*>(0x2000);
Carrier RealFind(void* l,void*,const char*,void*){assert(l==linker);++finds;return {0x3000};}
jmethodID RealEncode(void* m,void*){assert(m==manager||m==reinterpret_cast<void*>(0x7770));++encodes;return reinterpret_cast<jmethodID>(0x4000);}
void Suspend(void*,const char* cause,bool slow){assert(!suspended);++suspended;
 if(suspend_hook)suspend_hook(reinterpret_cast<void*>(0x8000),cause,slow);
}
void Resume(void*){assert(suspended==1);--suspended;}
class Hooks:public HookEngine {
 public:
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  if(std::strstr(name,"FindClass"))return !std::strcmp(mode,"missing")?nullptr:reinterpret_cast<void*>(RealFind);
  if(std::strstr(name,"EncodeMethodId"))return reinterpret_cast<void*>(RealEncode);
  if(std::strstr(name,"ThreadList"))return reinterpret_cast<void*>(RealSuspend);
  if(std::strstr(name,"C2"))return reinterpret_cast<void*>(Suspend);
  if(std::strstr(name,"D2"))return reinterpret_cast<void*>(Resume);
  assert(false);return nullptr;
 }
 void* Install(void* target,void* replacement,void** original,std::string*)override {
  assert(suspended==1);*original=target;
  if(target==reinterpret_cast<void*>(RealFind))find_hook=reinterpret_cast<Find>(replacement);
  else if(target==reinterpret_cast<void*>(RealEncode))encode_hook=reinterpret_cast<Encode>(replacement);
  else {assert(target==reinterpret_cast<void*>(RealSuspend));suspend_hook=reinterpret_cast<SuspendThreads>(replacement);}
  return target;
 }
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
jclass JNICALL PublicFind(JNIEnv*,const char* name){
 assert(!suspended && std::strstr(name,"CalibrationFixture"));
 const char* descriptor=!std::strcmp(mode,"bypassed")?"Ljava/lang/Object;":"Lcom/github/ytlog/artdcevm/internal/CalibrationFixture;";
 auto result=find_hook(linker,nullptr,descriptor,nullptr);assert(result.reference==0x3000);
 return reinterpret_cast<jclass>(0x5000);
}
jmethodID JNICALL PublicMethod(JNIEnv*,jclass,const char*,const char*){
 auto result=encode_hook(manager,nullptr);
 if(!std::strcmp(mode,"conflict"))encode_hook(reinterpret_cast<void*>(0x7770),nullptr);
 return result;
}
jboolean JNICALL Exception(JNIEnv*){return JNI_FALSE;}
void JNICALL Delete(JNIEnv*,jobject){}
jint JNICALL Vm(JNIEnv*,JavaVM** out){*out=reinterpret_cast<JavaVM*>(0x6000);return JNI_OK;}
}
int main(int argc,char** argv){
 if(argc>1)mode=argv[1];
 JNINativeInterface_ table{};table.FindClass=PublicFind;table.GetMethodID=PublicMethod;
 table.ExceptionCheck=Exception;table.DeleteLocalRef=Delete;table.GetJavaVM=Vm;
 JNIEnv env{&table};Hooks hooks;std::string error;
 RuntimeReceivers output{reinterpret_cast<void*>(0xaa),reinterpret_cast<void*>(0xbb),nullptr};
 bool ok=CalibrateRuntimeReceivers(hooks,&env,&output,&error);
 if(!std::strcmp(mode,"success")){
  assert(ok && error.empty() && output.class_linker==linker && output.jni_id_manager==manager && output.java_vm==reinterpret_cast<JavaVM*>(0x6000) && output.thread_list==reinterpret_cast<void*>(0x8000));
  assert(CalibrateRuntimeReceivers(hooks,&env,&output,&error));
  // The observation request is over: all unrelated callers remain passthrough.
  auto result=find_hook(linker,nullptr,"Lunrelated;",nullptr);assert(result.reference==0x3000);
  assert(encode_hook(manager,nullptr)==reinterpret_cast<jmethodID>(0x4000));
  assert(finds==3&&encodes==3);
  void* trampoline=nullptr;
  assert(SetRuntimeFindClassHandler(reinterpret_cast<void*>(Delegate),&trampoline));
  real_delegate=reinterpret_cast<Find>(trampoline);
  assert(find_hook(linker,nullptr,"Lunrelated;",nullptr).reference==0x3000 && delegated==1);
  assert(SetRuntimeFindClassHandler(nullptr,nullptr));
  assert(find_hook(linker,nullptr,"Lunrelated;",nullptr).reference==0x3000 && delegated==1);
 }else{assert(!ok && !error.empty() && output.class_linker==reinterpret_cast<void*>(0xaa)&&output.jni_id_manager==reinterpret_cast<void*>(0xbb));}
 assert(!suspended);
 std::cout<<"Runtime receiver probe "<<mode<<": passed\n";
}
