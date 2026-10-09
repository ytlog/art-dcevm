#include "platform/common/jni_receiver_calibration.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
using Service=artdcevm::jni_receiver_calibration::Service<sizeof(void*)>;
using Result=artdcevm::jni_receiver_calibration::Result;
using Callback=artdcevm::borrowed_jni_callback::Result;
using Scope=artdcevm::abi::common::RunnableJniScope;
Service service;
struct State {
  bool exception=false,runnable=false,skip=false,original_null=false,original_exception=false;
  bool wrong_self=false,wrong_env=false,wrong_vm=false,current_null=false;
  bool current_during=false,current_after=false,vm_error=false,vm_null=false,vm_second_wrong=false;
  bool vm_exception=false,throw_jni=false;
  int original_calls=0,current_calls=0,vm_calls=0,jni_calls=0;
};
thread_local State state;
int different_self;
JavaVM vm{},different_vm{};
struct Env {JNIEnv env;void* self;JavaVM* vm;};
static_assert(offsetof(Env,self)==sizeof(void*) && offsetof(Env,vm)==2*sizeof(void*));
Env other{};
jobject carrier=reinterpret_cast<jobject>(0x2000);
void* native_method=reinterpret_cast<void*>(0x3000);
jboolean Exception(JNIEnv*){return state.exception;}
jint GetVM(JNIEnv*,JavaVM** output) {
  ++state.vm_calls;
  if(state.vm_exception)state.exception=true;
  *output=state.vm_null?nullptr:(state.vm_second_wrong&&state.vm_calls==2?&different_vm:&vm);
  return state.vm_error?JNI_ERR:JNI_OK;
}
void* Current() {
  ++state.current_calls;
  if(state.current_null)return nullptr;
  if((state.current_during&&state.current_calls==2)||(state.current_after&&state.current_calls==3))return &different_self;
  return &state;
}
void* Original(const Scope&,jobject object) {
  assert(state.runnable && object==carrier);++state.original_calls;
  if(state.original_exception)state.exception=true;
  return state.original_null?nullptr:native_method;
}
jmethodID FromReflected(JNIEnv* env,jobject object) {
  ++state.jni_calls;
  const bool old=state.runnable;state.runnable=true;
  struct Restore {bool old;~Restore(){state.runnable=old;}} restore{old};
  if(state.throw_jni)throw std::runtime_error("fixture JNI throws");
  if(state.skip)return reinterpret_cast<jmethodID>(0x4000);
  Scope scope{state.wrong_self?static_cast<void*>(&different_self):static_cast<void*>(&state),
      state.wrong_env?&other.env:env,state.wrong_vm?&different_vm:&vm};
  void* method=service.OnFromReflectedMethod(scope,object);
  // Native return is preserved before ART encodes a distinct JNI ID.
  assert(method==(state.original_null?nullptr:native_method));
  return method?reinterpret_cast<jmethodID>(0x4000):nullptr;
}
struct Hooks:artdcevm::HookEngine {
  bool missing_current=false,missing_leaf=false;
  bool Initialize(std::string*)override{assert(false);return false;}
  void* FindSymbol(const char* library,const char* name)override {
    assert(std::strcmp(library,"libart.so")==0);
    if(std::strcmp(name,artdcevm::abi::common::kCurrentThread)==0)
      return missing_current?nullptr:reinterpret_cast<void*>(Current);
    assert(std::strcmp(name,artdcevm::abi::common::kFromReflectedMethod)==0);
    return missing_leaf?nullptr:reinterpret_cast<void*>(Original);
  }
  void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}
  bool Remove(void*,std::string*)override{assert(false);return false;}
};
int main() {
  JNINativeInterface_ functions{};
  functions.ExceptionCheck=Exception;functions.GetJavaVM=GetVM;functions.FromReflectedMethod=FromReflected;
  Env env{{&functions},&state,&vm};other={{&functions},&state,&vm};
  Hooks hooks;std::string error;
  assert(!Service::Bind(hooks,nullptr,&service,&error));
  assert(!Service::Bind(hooks,reinterpret_cast<void*>(Original),nullptr,&error));
  hooks.missing_current=true;assert(!Service::Bind(hooks,reinterpret_cast<void*>(Original),&service,&error));
  hooks.missing_current=false;hooks.missing_leaf=true;assert(!Service::Bind(hooks,reinterpret_cast<void*>(Original),&service,&error));
  hooks.missing_leaf=false;assert(Service::Bind(hooks,reinterpret_cast<void*>(Original),&service,&error));
  auto run=[&]{return service.Calibrate(&env.env,carrier,sizeof(env));};
  auto reset=[&]{state={};env.self=&state;env.vm=&vm;};
  auto first=run();assert(first.result==Result::kSuccess && first.original_callback_hits==1 && first.matched_owner_prefixes==2);
  assert(state.original_calls==1 && state.vm_calls==2 && state.current_calls==3 && !state.runnable);
  reset();int observed=0;
  auto observer=[](const artdcevm::borrowed_jni_callback::Context& context,void* opaque){
    assert(state.runnable&&context.original_method==native_method&&context.original_leaf==Original);
    ++*static_cast<int*>(opaque);return true;
  };
  assert(service.Calibrate(&env.env,carrier,sizeof(env),observer,&observed).result==Result::kSuccess&&observed==1&&!state.runnable);
  reset();auto refusal=[](const artdcevm::borrowed_jni_callback::Context&,void*){return false;};
  assert(service.Calibrate(&env.env,carrier,sizeof(env),refusal,nullptr).result==Result::kCallbackRejected&&!state.runnable);
  reset();auto throws=[](const artdcevm::borrowed_jni_callback::Context&,void*)->bool{throw std::runtime_error("metadata refusal");};
  assert(service.Calibrate(&env.env,carrier,sizeof(env),throws,nullptr).callback==Callback::kOperationThrew&&!state.runnable);
  reset();state.runnable=true;assert(run().result==Result::kSuccess);assert(state.runnable);
  reset();env.self=&different_self;assert(run().result==Result::kReceiverMismatch);assert(!state.jni_calls);
  reset();env.vm=&different_vm;assert(run().result==Result::kReceiverMismatch);assert(!state.jni_calls);
  reset();state.wrong_self=true;assert(run().result==Result::kCallbackRejected);assert(state.original_calls==1&&!state.runnable);
  reset();state.wrong_vm=true;assert(run().result==Result::kCallbackRejected);
  reset();state.wrong_env=true;auto report=run();assert(report.result==Result::kCallbackRejected && report.callback==Callback::kContextMismatch);
  reset();state.current_null=true;assert(run().result==Result::kCurrentUnavailable);assert(!state.jni_calls);
  reset();state.current_during=true;assert(run().result==Result::kCallbackRejected);
  reset();state.current_after=true;assert(run().result==Result::kCallbackRejected);
  reset();state.vm_second_wrong=true;assert(run().result==Result::kCallbackRejected);
  reset();state.vm_error=true;assert(run().result==Result::kVmUnavailable);assert(!state.jni_calls);
  reset();state.vm_null=true;assert(run().result==Result::kVmUnavailable);
  reset();state.vm_exception=true;assert(run().result==Result::kJniException);assert(state.exception&&!state.jni_calls);
  reset();state.exception=true;assert(run().result==Result::kPendingException);assert(!state.vm_calls&&!state.current_calls&&!state.jni_calls&&state.exception);
  reset();state.original_exception=true;assert(run().result==Result::kJniException);assert(state.original_calls==1&&state.exception&&!state.runnable);
  reset();state.original_null=true;report=run();assert(report.result==Result::kCallbackRejected&&report.callback==Callback::kOriginalRejected);
  reset();state.skip=true;report=run();assert(report.result==Result::kCallbackRejected&&report.callback==Callback::kLeafNotReached);
  reset();state.throw_jni=true;try{run();assert(false);}catch(const std::runtime_error&){}assert(!state.runnable);
  reset();assert(run().result==Result::kSuccess); // TLS restored after fixture C++ exception.
  reset();state.runnable=true;Scope ordinary{&state,&env.env,&vm};
  assert(service.OnFromReflectedMethod(ordinary,carrier)==native_method && state.original_calls==1 && !state.vm_calls);
  Service unbound;assert(unbound.Calibrate(&env.env,carrier,sizeof(env)).result==Result::kUnavailable);
  assert(service.Calibrate(nullptr,carrier,sizeof(env)).result==Result::kInvalidRequest);
  assert(service.Calibrate(&env.env,nullptr,sizeof(env)).result==Result::kInvalidRequest);
  reset();assert(service.Calibrate(&env.env,carrier,sizeof(env)-1).result==Result::kReceiverMismatch);
  std::cout<<"JNI receiver calibration: independent named owner, real JNI callback, original return, exception/state preservation and fail-closed boundaries passed\n";
}
