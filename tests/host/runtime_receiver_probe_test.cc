#include "platform/common/runtime_receiver_probe.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
using Carrier=abi::arm64::android11::ReleaseObjectPointer;
using Find=Carrier (*)(void*,void*,const char*,void*);
using LengthFind=Carrier (*)(void*,void*,const char*,size_t,void*);
using Encode=jmethodID (*)(void*,void*);
namespace {
using SuspendThreads=void (*)(void*,const char*,bool);
SuspendThreads suspend_hook;
void RealSuspend(void* list,const char*,bool){assert(list==reinterpret_cast<void*>(0x8000));}
Find find_hook;Find real_delegate;int delegated=0;
LengthFind length_hook,length_delegate;
size_t forwarded_length=0;
Carrier LengthDelegate(void* l,void* t,const char* d,size_t n,void* h){++delegated;return length_delegate(l,t,d,n,h);}
Carrier Delegate(void* l,void* t,const char* d,void* h){++delegated;return real_delegate(l,t,d,h);}
Encode encode_hook;int suspended=0,finds=0,encodes=0;
const char* mode="success";
void* linker=reinterpret_cast<void*>(0x1000);
void* manager=reinterpret_cast<void*>(0x2000);
Carrier RealFind(void* l,void*,const char*,void*){assert(l==linker);++finds;return {0x3000};}
Carrier RealLengthFind(void* l,void* t,const char* d,size_t n,void* h){forwarded_length=n;return RealFind(l,t,d,h);}
jmethodID RealEncode(void* m,void*){assert(m==manager||m==reinterpret_cast<void*>(0x7770));++encodes;return reinterpret_cast<jmethodID>(0x4000);}
void Suspend(void*,const char* cause,bool slow){assert(!suspended);++suspended;
 if(suspend_hook)suspend_hook(reinterpret_cast<void*>(0x8000),cause,slow);
}
void Resume(void*){assert(suspended==1);--suspended;}
class Hooks:public HookEngine {
 public:
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  if(std::strstr(name,"FindClass")) {
    if(!std::strcmp(mode,"missing"))return nullptr;
    if(!std::strcmp(mode,"length")||!std::strcmp(mode,"length-bypassed"))
      return std::strstr(name,"EPKcm")?reinterpret_cast<void*>(RealLengthFind):nullptr;
    return reinterpret_cast<void*>(RealFind);
  }
  if(std::strstr(name,"EncodeMethodId"))return reinterpret_cast<void*>(RealEncode);
  if(std::strstr(name,"ThreadList"))return reinterpret_cast<void*>(RealSuspend);
  if(std::strstr(name,"C2"))return reinterpret_cast<void*>(Suspend);
  if(std::strstr(name,"D2"))return reinterpret_cast<void*>(Resume);
  assert(false);return nullptr;
 }
 void* Install(void* target,void* replacement,void** original,std::string*)override {
  assert(suspended==1);*original=target;
  if(target==reinterpret_cast<void*>(RealFind))find_hook=reinterpret_cast<Find>(replacement);
  else if(target==reinterpret_cast<void*>(RealLengthFind))length_hook=reinterpret_cast<LengthFind>(replacement);
  else if(target==reinterpret_cast<void*>(RealEncode))encode_hook=reinterpret_cast<Encode>(replacement);
  else if(target==reinterpret_cast<void*>(RealSuspend))suspend_hook=reinterpret_cast<SuspendThreads>(replacement);
  else {assert(false);}
  return target;
 }
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
jclass JNICALL PublicFind(JNIEnv*,const char* name){
 assert(!suspended && std::strstr(name,"CalibrationFixture"));
 const char* descriptor=!std::strcmp(mode,"bypassed")?"Ljava/lang/Object;":"Lcom/github/ytlog/artdcevm/internal/CalibrationFixture;";
 auto result=length_hook?length_hook(linker,nullptr,descriptor,std::strlen(descriptor)+(!std::strcmp(mode,"length-bypassed")?1:0),nullptr):find_hook(linker,nullptr,descriptor,nullptr);assert(result.reference==0x3000);
 return reinterpret_cast<jclass>(0x5000);
}
jmethodID JNICALL PublicMethod(JNIEnv*,jclass,const char*,const char*){
 auto result=encode_hook(manager,nullptr);
 if(!std::strcmp(mode,"conflict"))encode_hook(reinterpret_cast<void*>(0x7770),nullptr);
 return result;
}
jboolean JNICALL Exception(JNIEnv*){return JNI_FALSE;}
void JNICALL Delete(JNIEnv*,jobject){}
JNIInvokeInterface_ vm_table{};
JavaVM vm{&vm_table};
jint JNICALL Vm(JNIEnv*,JavaVM** out){*out=&vm;return JNI_OK;}
}
int main(int argc,char** argv){
 if(argc>1)mode=argv[1];
 JNINativeInterface_ table{};table.FindClass=PublicFind;table.GetMethodID=PublicMethod;
 table.ExceptionCheck=Exception;table.DeleteLocalRef=Delete;table.GetJavaVM=Vm;
 JNIEnv env{&table};Hooks hooks;std::string error;
 runtime_receiver::Receivers output{reinterpret_cast<void*>(0xaa),reinterpret_cast<void*>(0xbb),nullptr};
 if(!std::strcmp(mode,"length")||!std::strcmp(mode,"length-bypassed")) {
  runtime_receiver::Receivers core{reinterpret_cast<void*>(0xaa)};
  const bool ok=runtime_receiver::Calibrate(hooks,&env,&core,&error);
  if(!std::strcmp(mode,"length-bypassed")) {
    assert(!ok&&!error.empty()&&core.class_linker==reinterpret_cast<void*>(0xaa));
  } else {
    assert(ok&&core.class_linker==linker&&core.thread_list==reinterpret_cast<void*>(0x8000));
    assert(!runtime_receiver::SetFindClassHandler(reinterpret_cast<void*>(Delegate),nullptr));
    void* trampoline=nullptr;
    assert(runtime_receiver::SetLengthFindClassHandler(reinterpret_cast<void*>(LengthDelegate),&trampoline));
    length_delegate=reinterpret_cast<LengthFind>(trampoline);
    assert(length_hook(linker,nullptr,"Lunrelated;",0x1234,nullptr).reference==0x3000);
    assert(delegated==1&&forwarded_length==0x1234);
    assert(runtime_receiver::SetLengthFindClassHandler(nullptr,nullptr));
    assert(length_hook(linker,nullptr,"Lunrelated;",27,nullptr).reference==0x3000);
    assert(delegated==1&&forwarded_length==27);
  }
  assert(!suspended);std::cout<<"Runtime receiver probe "<<mode<<": passed\n";return 0;
 }
 bool ok=runtime_receiver::Calibrate(hooks,&env,&output,&error);
 if(!std::strcmp(mode,"success")){
  assert(ok && error.empty() && output.class_linker==linker && output.jni_id_manager==manager && output.java_vm==&vm && output.thread_list==reinterpret_cast<void*>(0x8000));
  assert(runtime_receiver::Calibrate(hooks,&env,&output,&error));
  // The observation request is over: all unrelated callers remain passthrough.
  auto result=find_hook(linker,nullptr,"Lunrelated;",nullptr);assert(result.reference==0x3000);
  assert(encode_hook(manager,nullptr)==reinterpret_cast<jmethodID>(0x4000));
  assert(finds==3&&encodes==3);
  void* trampoline=nullptr;
  assert(runtime_receiver::SetFindClassHandler(reinterpret_cast<void*>(Delegate),&trampoline));
  real_delegate=reinterpret_cast<Find>(trampoline);
  assert(find_hook(linker,nullptr,"Lunrelated;",nullptr).reference==0x3000 && delegated==1);
  assert(runtime_receiver::SetFindClassHandler(nullptr,nullptr));
  assert(find_hook(linker,nullptr,"Lunrelated;",nullptr).reference==0x3000 && delegated==1);
 }else{
  assert(!ok && !error.empty() && output.class_linker==reinterpret_cast<void*>(0xaa)&&output.jni_id_manager==reinterpret_cast<void*>(0xbb));

 }
 assert(!suspended);
 std::cout<<"Runtime receiver probe "<<mode<<": passed\n";
}
