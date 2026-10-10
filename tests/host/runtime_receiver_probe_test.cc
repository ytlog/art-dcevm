#include "platform/common/runtime_receiver_probe.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include "platform/abi/common/jni_id_query_scope.h"
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
bool in_public_find=false;int query_live=0,query_balanced=0;
void* linker=reinterpret_cast<void*>(0x1000);
void* manager=reinterpret_cast<void*>(0x2000);
void QueryConstruct(abi::common::JniIdQueryScope* scope){
 assert(in_public_find && !suspended && !query_live);++query_live;
 scope->manager=!std::strcmp(mode,"scope-null")?nullptr:manager;
 if(!std::strcmp(mode,"scope-conflict")||(!std::strcmp(mode,"scope-switch")&&query_balanced))scope->manager=reinterpret_cast<void*>(0x7770);
}
void QueryDestroy(abi::common::JniIdQueryScope*){
 assert(in_public_find && !suspended && query_live==1);--query_live;++query_balanced;
}
Carrier RealFind(void* l,void* t,const char* d,void* h){
 assert(l==linker);++finds;
 if(!std::strcmp(mode,"dual-nested"))return length_hook(l,t,d,std::strlen(d),h);
 return {0x3000};
}
Carrier RealLengthFind(void* l,void*,const char*,size_t n,void*){assert(l==linker);forwarded_length=n;++finds;return {0x3000};}
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
    if(!std::strcmp(mode,"length")||!std::strcmp(mode,"length-bypassed")||!std::strcmp(mode,"scope-length-pointer"))
      return std::strstr(name,"EPKcm")?reinterpret_cast<void*>(RealLengthFind):nullptr;
    if(std::strstr(mode,"dual-"))
      return std::strstr(name,"EPKcm")?reinterpret_cast<void*>(RealLengthFind):reinterpret_cast<void*>(RealFind);
    return std::strstr(name,"EPKcm")?nullptr:reinterpret_cast<void*>(RealFind);
  }
  if(std::strstr(name,"ScopedEnableSuspendAllJniIdQueries")){
    if(!std::strncmp(mode,"scope-",6)) {
      if(std::strstr(name,"D2")&&(!std::strcmp(mode,"scope-unpaired")||!std::strcmp(mode,"scope-no-route")))return nullptr;
      return std::strstr(name,"C2")?reinterpret_cast<void*>(QueryConstruct):reinterpret_cast<void*>(QueryDestroy);
    }
    return nullptr;
  }
  if(std::strstr(name,"EncodeMethodId"))return (!std::strcmp(mode,"scope-only")||!std::strcmp(mode,"scope-no-route"))?nullptr:reinterpret_cast<void*>(RealEncode);
  if(std::strstr(name,"ThreadList"))return reinterpret_cast<void*>(RealSuspend);
  if(std::strstr(name,"C2"))return reinterpret_cast<void*>(Suspend);
  if(std::strstr(name,"D2"))return reinterpret_cast<void*>(Resume);
  assert(false);return nullptr;
 }
 void* Install(void* target,void* replacement,void** original,std::string*)override {
  assert(suspended==1);
  if(!std::strcmp(mode,"dual-install-failure")&&target==reinterpret_cast<void*>(RealLengthFind))return nullptr;
  *original=target;
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
 const char* descriptor=(!std::strcmp(mode,"bypassed")||!std::strcmp(mode,"dual-bypassed"))?"Ljava/lang/Object;":"Lcom/github/ytlog/artdcevm/internal/CalibrationFixture;";
 in_public_find=true;
 auto result=length_hook&&std::strcmp(mode,"dual-plain")&&std::strcmp(mode,"dual-nested")?length_hook(linker,nullptr,descriptor,std::strlen(descriptor)+(!std::strcmp(mode,"length-bypassed")?1:0),nullptr):find_hook(linker,nullptr,descriptor,nullptr);assert(result.reference==0x3000);in_public_find=false;
 return reinterpret_cast<jclass>(0x5000);
}
jmethodID JNICALL PublicMethod(JNIEnv*,jclass,const char*,const char*){
 if(!std::strcmp(mode,"scope-dual-length")||!std::strcmp(mode,"scope-switch")||!std::strcmp(mode,"scope-length-pointer")||!std::strcmp(mode,"scope-pointer")||!std::strcmp(mode,"scope-only")||!std::strcmp(mode,"scope-null")||!std::strcmp(mode,"scope-unpaired"))return reinterpret_cast<jmethodID>(0x4000);
 auto result=encode_hook(manager,nullptr);
 if(!std::strcmp(mode,"conflict")||!std::strcmp(mode,"dual-conflict"))encode_hook(reinterpret_cast<void*>(0x7770),nullptr);
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
 if(!std::strcmp(mode,"scope-dual-length")||!std::strcmp(mode,"scope-length-pointer")||!std::strcmp(mode,"scope-pointer")||!std::strcmp(mode,"scope-only")||!std::strcmp(mode,"scope-crosscheck")){
  assert(ok&&error.empty()&&output.jni_id_manager==manager&&output.class_linker==linker);
  assert(query_balanced==2&&!query_live);
  assert(encodes==(!std::strcmp(mode,"scope-crosscheck")?1:0));
  assert(runtime_receiver::Calibrate(hooks,&env,&output,&error));
  assert(query_balanced==4&&!query_live);
 }else if(!std::strcmp(mode,"dual-length")||!std::strcmp(mode,"dual-plain")||!std::strcmp(mode,"dual-nested")){
  assert(ok&&error.empty()&&output.class_linker==linker&&output.jni_id_manager==manager);
  assert(find_hook&&length_hook);
  void* plain=nullptr;void* length=nullptr;
  assert(runtime_receiver::SetFindClassHandler(reinterpret_cast<void*>(Delegate),&plain));
  assert(runtime_receiver::SetLengthFindClassHandler(reinterpret_cast<void*>(LengthDelegate),&length));
  real_delegate=reinterpret_cast<Find>(plain);length_delegate=reinterpret_cast<LengthFind>(length);
  assert(length_hook(linker,nullptr,"Lunrelated;",0x1234,nullptr).reference==0x3000);
  assert(forwarded_length==0x1234&&delegated==1);
  assert(runtime_receiver::SetFindClassHandler(nullptr,nullptr));
  assert(runtime_receiver::SetLengthFindClassHandler(nullptr,nullptr));
 }else if(!std::strcmp(mode,"success")){
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
 if(!std::strcmp(mode,"scope-switch")||!std::strcmp(mode,"scope-null")||!std::strcmp(mode,"scope-conflict")){
  assert(query_balanced==2&&!query_live&&error.find("queryScope=2")!=std::string::npos);
 }
 if(!std::strcmp(mode,"scope-unpaired"))assert(query_balanced==0&&error.find("queryScope=0")!=std::string::npos);
 if(!std::strcmp(mode,"scope-no-route"))assert(query_balanced==0&&error.find("Missing named")!=std::string::npos);
 assert(!suspended);
 std::cout<<"Runtime receiver probe "<<mode<<": passed\n";
}
