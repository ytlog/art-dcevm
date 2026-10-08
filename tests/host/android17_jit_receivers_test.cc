#include "platform/android17/jit_receivers.h"
#include "platform/common/runnable_executor.h"
#include <array>
#include <jvmti.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <cstdarg>
using namespace artdcevm;
namespace {
const char* mode="success";
using Enqueue=void(*)(void*,void*,void*);
using Collect=void(*)(void*);
using HeapGc=void(*)(void*,bool,int);
Enqueue enqueue_proxy;Collect cache_proxy;HeapGc heap_proxy;
int jit_object,cache_object,heap_object,self,handle[3],suspends=0,warmups=0,collections=0,mutations=0,disposed=0;
alignas(8) std::array<unsigned char,32> native_method{};
JNIEnv* current_env;JavaVM* current_vm;
jvmtiEnv* current_jvmti;
void EnqueueOriginal(void* receiver,void* method,void* thread){
  assert(receiver==&jit_object&&method==native_method.data()&&thread==&self);++warmups;
}
void CacheOriginal(void* receiver){assert(receiver==&cache_object);++mutations;}
void HeapOriginal(void* receiver,bool clear,int cause){assert(receiver==&heap_object&&!clear&&cause==9);++collections;}
void Construct(void*,const char*,bool long_suspend){assert(!long_suspend&&suspends==0);++suspends;}
void Destroy(void*){assert(suspends==1);--suspends;}
void* Leaf(const abi::arm64::android_common::RunnableJniScope& scope,jobject object){
 assert(scope.self==&self&&scope.env==current_env&&scope.vm==current_vm&&object==reinterpret_cast<jobject>(&handle[1]));
 return native_method.data();
}
class Hooks:public HookEngine{
 public:
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  if(std::strstr(name,"MaybeEnqueueCompilation"))return !std::strcmp(mode,"missing")?nullptr:reinterpret_cast<void*>(EnqueueOriginal);
  if(std::strstr(name,"InvalidateAllCompiledCode"))return reinterpret_cast<void*>(CacheOriginal);
  if(std::strstr(name,"CollectGarbage"))return reinterpret_cast<void*>(HeapOriginal);
  if(std::strstr(name,"FromReflectedMethod"))return reinterpret_cast<void*>(Leaf);
  if(std::strstr(name,"C2"))return reinterpret_cast<void*>(Construct);
  if(std::strstr(name,"D2"))return reinterpret_cast<void*>(Destroy);
  assert(false);return nullptr;
 }
 void* Install(void* target,void* replacement,void** original,std::string*)override{
  assert(suspends==1);*original=target;
  if(target==reinterpret_cast<void*>(EnqueueOriginal))enqueue_proxy=reinterpret_cast<Enqueue>(replacement);
  else if(target==reinterpret_cast<void*>(CacheOriginal))cache_proxy=reinterpret_cast<Collect>(replacement);
  else {assert(target==reinterpret_cast<void*>(HeapOriginal));heap_proxy=reinterpret_cast<HeapGc>(replacement);}
  return target;
 }
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
jboolean Exception(JNIEnv*){return false;}
jobjectRefType RefType(JNIEnv*,jobject){return JNIGlobalRefType;}
jmethodID RefMethod(JNIEnv*,jobject){return reinterpret_cast<jmethodID>(&handle[1]);}
jmethodID StaticMethod(JNIEnv*,jclass,const char* name,const char* descriptor){
 assert(!std::strcmp(name,"hotLoop")&&!std::strcmp(descriptor,"(I)I"));return reinterpret_cast<jmethodID>(&handle[1]);
}
jint CallStatic(JNIEnv*,jclass,jmethodID method,va_list arguments){
 assert(!suspends&&method==reinterpret_cast<jmethodID>(&handle[1])&&va_arg(arguments,int)==100000);
 enqueue_proxy(&jit_object,native_method.data(),&self);return 42;
}
jint GetEnv(JavaVM*,void** out,jint version){assert(version==JVMTI_VERSION_1_2);*out=current_jvmti;return JNI_OK;}
jvmtiError ForceGc(jvmtiEnv*){heap_proxy(&heap_object,false,9);return JVMTI_ERROR_NONE;}
jvmtiError Capabilities(jvmtiEnv*,const jvmtiCapabilities* caps){assert(caps->can_redefine_classes);return JVMTI_ERROR_NONE;}
jvmtiError Dispose(jvmtiEnv*){++disposed;return JVMTI_ERROR_NONE;}
jvmtiError Deallocate(jvmtiEnv*,unsigned char*){return JVMTI_ERROR_NONE;}
jvmtiError Redefine(jvmtiEnv*,jint count,const jvmtiClassDefinition* definitions){
 assert(count==1&&definitions->class_byte_count==1&&definitions->class_bytes[0]==7);
 if(std::strcmp(mode,"cache-missing"))cache_proxy(&cache_object);
 return JVMTI_ERROR_NONE;
}
jvmtiError Extensions(jvmtiEnv*,jint* count,jvmtiExtensionFunctionInfo** output){
 static jvmtiExtensionFunctionInfo extension{};
 extension.id=const_cast<char*>("com.android.art.class.structurally_redefine_classes");
 extension.func=reinterpret_cast<jvmtiExtensionFunction>(Redefine);
 *count=1;*output=&extension;return JVMTI_ERROR_NONE;
}
}
namespace artdcevm::runnable_executor {
runnable_jni::Result With(JNIEnv* env,jobject,runnable_jni::Operation operation,void* data){
 const runnable_jni::Context context{&self,env,current_vm};
 return operation(context,data)?runnable_jni::Result::kSuccess:runnable_jni::Result::kOperationRejected;
}
}
int main(int argc,char** argv){
 if(argc>1)mode=argv[1];
 JNINativeInterface_ jni{};jni.ExceptionCheck=Exception;jni.GetObjectRefType=RefType;
 jni.FromReflectedMethod=RefMethod;jni.GetStaticMethodID=StaticMethod;jni.CallStaticIntMethodV=CallStatic;
 JNIEnv env{&jni};current_env=&env;
 JNIInvokeInterface_ invoke{};invoke.GetEnv=GetEnv;JavaVM vm{&invoke};current_vm=&vm;
 jvmtiInterface_1_ jvmti{};jvmti.ForceGarbageCollection=ForceGc;jvmti.AddCapabilities=Capabilities;
 jvmti.GetExtensionFunctions=Extensions;jvmti.Deallocate=Deallocate;jvmti.DisposeEnvironment=Dispose;
 jvmtiEnv temporary{&jvmti};current_jvmti=&temporary;
 abi::arm64::android15::RuntimePrefix runtime{};
 runtime.jit=&jit_object;runtime.jit_code_cache=&cache_object;runtime.heap=&heap_object;
 runtime.class_linker=&handle[0];runtime.jni_id_manager=&handle[1];runtime.thread_list=&handle[2];runtime.java_vm=&vm;
 android17::RuntimeServices core{&runtime,&handle[0],{}};
 core.receivers.class_linker=runtime.class_linker;core.receivers.jni_id_manager=runtime.jni_id_manager;
 core.receivers.thread_list=runtime.thread_list;core.receivers.java_vm=&vm;
 uint32_t index=27,flags=9;std::memcpy(native_method.data()+8,&index,4);std::memcpy(native_method.data()+4,&flags,4);
 method_identity::Oracle oracle{reinterpret_cast<jobject>(&handle[1]),index,flags};
 method_identity::Calibration model{method_identity::Model::kMethod12Onward,{{1,1},{8,2},{0x102,3}},{32,32,32}};
 if(!std::strcmp(mode,"wrong-identity"))++oracle.dex_method_index;
 if(!std::strcmp(mode,"wrong-graph"))runtime.jit_code_cache=&handle[0];
 Hooks hooks;runtime_services::View output;output.heap=reinterpret_cast<void*>(77);
 unsigned char dex=7;std::string error;
 const bool result=android17::CalibrateJitReceivers(hooks,&env,reinterpret_cast<jobject>(&handle[0]),oracle,model,core,
    reinterpret_cast<jclass>(&handle[2]),&dex,1,&output,&error);
 if(!std::strcmp(mode,"success")){
  assert(result&&error.empty()&&output.jit==&jit_object&&output.jit_code_cache==&cache_object&&output.heap==&heap_object);
  assert(warmups==32&&collections==1&&mutations==1&&disposed==1);
 }else{
  assert(!result&&!error.empty()&&output.heap==reinterpret_cast<void*>(77));
  if(!std::strcmp(mode,"wrong-identity"))assert(!warmups&&!collections&&!mutations&&!disposed);
 }
 assert(!suspends);
 std::cout<<"Android17 JIT receivers: "<<mode<<" passed\n";
}
