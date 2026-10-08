#include "platform/common/runnable_jni_bridge.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <stdexcept>
using namespace artdcevm::runnable_jni;
using Scope=artdcevm::abi::arm64::android_common::RunnableJniScope;
struct State {bool runnable=false,exception=false,skip=false,mismatch=false,original_null=false,original_exception=false,duplicate=false,throw_jni=false;int originals=0,operations=0,transitions=0,decodes=0;};
thread_local State state;
Bridge bridge;GlobalDecoder decoder;int vm;
jobject carrier=reinterpret_cast<jobject>(0x1000);
jobject global=reinterpret_cast<jobject>(0x2000),local=reinterpret_cast<jobject>(0x3000),weak=reinterpret_cast<jobject>(0x4000);
jboolean Exception(JNIEnv*){return state.exception;}
jobjectRefType RefType(JNIEnv*,jobject object){assert(state.runnable);return object==global?JNIGlobalRefType:object==local?JNILocalRefType:object==weak?JNIWeakGlobalRefType:JNIInvalidRefType;}
void* Original(const Scope&,jobject){assert(state.runnable);++state.originals;if(state.original_exception)state.exception=true;return state.original_null?nullptr:reinterpret_cast<void*>(0x5000);}
artdcevm::abi::arm64::android14::ReleaseObjectPointer Decode(const void* self,jobject object){assert(state.runnable&&self==&state&&object==global);++state.decodes;return {0x6000};}
jmethodID FromReflected(JNIEnv* env,jobject object){
 bool old=state.runnable;state.runnable=true;if(!old)++state.transitions;
 struct Restore{bool old;~Restore(){state.runnable=old;}} restore{old};
 if(state.throw_jni)throw std::runtime_error("jni fixture");
 if(state.skip)return reinterpret_cast<jmethodID>(0x7000);
 Scope scope{&state,state.mismatch?nullptr:env,&vm};
 void* original=bridge.OnFromReflectedMethod(scope,object);
 if(state.duplicate)assert(bridge.OnFromReflectedMethod(scope,object)==original);
 // ART's returned JNI ID may be encoded and need not equal the leaf pointer.
 return original?reinterpret_cast<jmethodID>(0x7000):nullptr;
}
bool Success(const Context& context,void*){
 assert(state.runnable&&context.self==&state&&context.vm==&vm);++state.operations;
 void* result=reinterpret_cast<void*>(0x9999);
 assert(decoder.Decode(context,global,&result)&&result==reinterpret_cast<void*>(0x6000));
 assert(decoder.Decode(context,nullptr,&result)&&!result);
 result=reinterpret_cast<void*>(0x9999);
 assert(!decoder.Decode(context,local,&result)&&result==reinterpret_cast<void*>(0x9999));
 assert(!decoder.Decode(context,weak,&result)&&result==reinterpret_cast<void*>(0x9999));
 auto bad=context;bad.vm=nullptr;assert(!decoder.Decode(bad,global,&result));
 assert(bridge.Invoke(context.env,carrier,Success,nullptr)==Result::kReentrant);
 // Nested ordinary JNI calls remain Runnable, preserve original calls, and never
 // dispatch this pending operation a second time.
 assert(context.env->FromReflectedMethod(carrier));
 return true;
}
bool Reject(const Context&,void*){++state.operations;return false;}
bool Throw(const Context&,void*){++state.operations;throw std::runtime_error("operation");}
bool JavaException(const Context&,void*){++state.operations;state.exception=true;return true;}
int main(){
 JNINativeInterface_ functions{};functions.ExceptionCheck=Exception;functions.FromReflectedMethod=FromReflected;functions.GetObjectRefType=RefType;
 JNIEnv env{&functions};
 assert(Bridge::Bind(reinterpret_cast<void*>(Original),&bridge));
 assert(GlobalDecoder::Bind(GlobalDecoder::Representation::kMatchedReleaseNoPoison,reinterpret_cast<void*>(Decode),&decoder));
 assert(!GlobalDecoder::Bind(static_cast<GlobalDecoder::Representation>(99),reinterpret_cast<void*>(Decode),&decoder));
 assert(!Bridge::Bind(nullptr,&bridge));
 assert(bridge.Invoke(&env,carrier,Success,nullptr)==Result::kSuccess);
 assert(!state.runnable&&state.operations==1&&state.originals==2&&state.transitions==1&&state.decodes==1);
 state={};state.runnable=true;assert(bridge.Invoke(&env,carrier,Success,nullptr)==Result::kSuccess);assert(state.runnable&&state.transitions==0);
 state={};state.exception=true;assert(bridge.Invoke(&env,carrier,Success,nullptr)==Result::kPendingException);assert(!state.originals&&state.exception);
 state={};state.skip=true;assert(bridge.Invoke(&env,carrier,Success,nullptr)==Result::kLeafNotReached);assert(!state.operations);
 state={};state.mismatch=true;assert(bridge.Invoke(&env,carrier,Success,nullptr)==Result::kContextMismatch);assert(state.originals==1&&!state.operations);
 state={};state.original_null=true;assert(bridge.Invoke(&env,carrier,Success,nullptr)==Result::kOriginalRejected);assert(!state.operations);
 state={};state.original_exception=true;assert(bridge.Invoke(&env,carrier,Success,nullptr)==Result::kJniException);assert(state.exception&&!state.operations&&!state.runnable);
 state={};assert(bridge.Invoke(&env,carrier,Reject,nullptr)==Result::kOperationRejected);assert(state.operations==1&&!state.runnable);
 state={};assert(bridge.Invoke(&env,carrier,Throw,nullptr)==Result::kOperationThrew);assert(state.operations==1&&!state.runnable);
 state={};assert(bridge.Invoke(&env,carrier,JavaException,nullptr)==Result::kJniException);assert(state.exception&&!state.runnable);
 state={};state.throw_jni=true;try{bridge.Invoke(&env,carrier,Success,nullptr);assert(false);}catch(const std::runtime_error&){}assert(!state.runnable);
 state={};assert(bridge.Invoke(&env,carrier,Success,nullptr)==Result::kSuccess); // TLS restored after throw.
 state={};state.duplicate=true;assert(bridge.Invoke(&env,carrier,Reject,nullptr)==Result::kOperationRejected);assert(state.operations==1&&state.originals==2);
 state={};state.runnable=true;Scope scope{&state,&env,&vm};assert(bridge.OnFromReflectedMethod(scope,carrier)==reinterpret_cast<void*>(0x5000));assert(!state.operations&&state.originals==1);state={};
 std::thread worker([&]{JNIEnv worker_env{&functions};assert(bridge.Invoke(&worker_env,carrier,Success,nullptr)==Result::kSuccess);assert(state.operations==1&&!state.runnable);});worker.join();assert(!state.operations);
 assert(bridge.Invoke(nullptr,carrier,Success,nullptr)==Result::kInvalidRequest);
 puts("Runnable JNI bridge: original scopes/returns, TLS reentrancy/restore, pending exceptions, global-only decode and per-thread dispatch passed");
}
