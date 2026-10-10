#include "platform/common/runnable_executor.h"
#include "platform/common/runnable_global_ref_bridge.h"
#include <cassert>
#include <cstring>
#include <cstdarg>
#include <iostream>
using namespace artdcevm;
using namespace runnable_jni;
namespace {
const char* mode="global";int suspended=0,callbacks=0;
void* self=reinterpret_cast<void*>(0x1000);
jobject thread=reinterpret_cast<jobject>(0x2000),carrier=reinterpret_cast<jobject>(0x3000);
JavaVM* vm=reinterpret_cast<JavaVM*>(0x4000);
GlobalRefBridge::Leaf global_hook;
Bridge::Leaf leaf_hook;
void* Current(){return self;}
void* Peer(void*){return reinterpret_cast<void*>(thread);}
abi::arm64::android11::ReleaseObjectPointer Decode(const void*,jobject value){return {std::strcmp(mode,"peer-mismatch")?reinterpret_cast<uintptr_t>(value):0x9990};}
jobject OriginalGlobal(void* value,void* actual,abi::arm64::android11::ReleaseObjectPointer object){assert(value==vm&&actual==self);return reinterpret_cast<jobject>(object.reference);}
void* OriginalLeaf(const abi::common::RunnableJniScope&,jobject){return reinterpret_cast<void*>(0x5000);}
void Suspend(void*,const char*,bool){assert(!suspended);++suspended;}
void Resume(void*){assert(suspended==1);--suspended;}
class Hooks:public HookEngine{
 public:
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  if(std::strstr(name,"DecodeGlobalJObject"))return !std::strcmp(mode,"missing")?nullptr:reinterpret_cast<void*>(Decode);
  if(std::strstr(name,"DecodeJObject"))return nullptr;
  if(std::strstr(name,"GetPeerFromOtherThread"))return std::strstr(name,"_ZNK")?nullptr:reinterpret_cast<void*>(Peer);
  if(std::strstr(name,"CurrentFromGdb"))return reinterpret_cast<void*>(Current);
  if(std::strstr(name,"AddGlobalRef"))return reinterpret_cast<void*>(OriginalGlobal);
  if(std::strstr(name,"FromReflectedMethod"))return reinterpret_cast<void*>(OriginalLeaf);
  if(std::strstr(name,"C2"))return reinterpret_cast<void*>(Suspend);
  if(std::strstr(name,"D2"))return reinterpret_cast<void*>(Resume);
  assert(false);return nullptr;
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override{
  assert(suspended==1);*original=target;
  if(target==reinterpret_cast<void*>(OriginalGlobal))global_hook=reinterpret_cast<GlobalRefBridge::Leaf>(proxy);
  else leaf_hook=reinterpret_cast<Bridge::Leaf>(proxy);
  return target;
 }
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
jboolean JNICALL Exception(JNIEnv*){return JNI_FALSE;}
jclass JNICALL Find(JNIEnv*,const char*){assert(!suspended);return reinterpret_cast<jclass>(0x6000);}
jmethodID JNICALL Method(JNIEnv*,jclass,const char*,const char*){return reinterpret_cast<jmethodID>(0x7000);}
jobject JNICALL Reflect(JNIEnv*,jclass,jmethodID,jboolean){return carrier;}
jobject JNICALL CallStatic(JNIEnv*,jclass,jmethodID,va_list){return thread;}
jobject JNICALL Global(JNIEnv*,jobject value){
 assert(!suspended);
 if(global_hook && std::strcmp(mode,"fallback"))return global_hook(vm,self,{reinterpret_cast<uintptr_t>(value)});
 return value;
}
void JNICALL Delete(JNIEnv*,jobject){}
jobjectRefType JNICALL Kind(JNIEnv*,jobject){return JNIGlobalRefType;}
jint JNICALL Vm(JNIEnv*,JavaVM** result){*result=vm;return JNI_OK;}
jboolean JNICALL Same(JNIEnv*,jobject a,jobject b){return a==b;}
jmethodID JNICALL FromReflect(JNIEnv* env,jobject value){
 abi::common::RunnableJniScope scope{self,env,vm};
 return reinterpret_cast<jmethodID>(leaf_hook(scope,value));
}
bool ExecuteOperation(const Context& context,void*){
 ++callbacks;void* decoded=nullptr;
 assert(runnable_executor::DecodeStrongGlobal(context,thread,&decoded)&&decoded==reinterpret_cast<void*>(thread));
 assert(runnable_executor::With(context.env,thread,ExecuteOperation,nullptr)==Result::kReentrant);
 return true;
}
}
int main(int argc,char** argv){
 if(argc>1)mode=argv[1];
 JNINativeInterface_ table{};table.ExceptionCheck=Exception;table.FindClass=Find;
 table.GetMethodID=Method;table.GetStaticMethodID=Method;table.ToReflectedMethod=Reflect;
 table.CallStaticObjectMethodV=CallStatic;table.NewGlobalRef=Global;table.DeleteLocalRef=Delete;
 table.DeleteGlobalRef=Delete;table.GetObjectRefType=Kind;table.GetJavaVM=Vm;
 table.IsSameObject=Same;table.FromReflectedMethod=FromReflect;JNIEnv env{&table};
 Hooks hooks;std::string error;bool ok=runnable_executor::Initialize(hooks,&env,&error);
 if(!std::strcmp(mode,"missing")||!std::strcmp(mode,"peer-mismatch")){
  assert(!ok&&!runnable_executor::Available());
  assert(runnable_executor::With(&env,thread,ExecuteOperation,nullptr)==Result::kUnavailable);
 }else{
  assert(ok&&runnable_executor::Available());assert(runnable_executor::Initialize(hooks,&env,&error));
  assert(runnable_executor::With(&env,thread,ExecuteOperation,nullptr)==Result::kSuccess);
  assert(callbacks==1);
  assert(!std::strcmp(runnable_executor::Route(),!std::strcmp(mode,"fallback")?"from-reflected-method":"new-global-ref"));
 }
 assert(!suspended);std::cout<<"Production Runnable executor "<<mode<<": passed\n";
}
