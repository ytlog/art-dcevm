#include "platform/common/native_scope_executor.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <limits>
namespace artdcevm::native_scope_executor {
namespace {
std::atomic<uint64_t> sequence{1};
uint64_t NextNonce() noexcept {
 uint64_t value=sequence.load(std::memory_order_relaxed);
 const auto maximum=uint64_t(std::numeric_limits<jlong>::max());
 while(value&&value<=maximum){if(sequence.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value;}
 return 0;
}
constexpr char bridge[]="com/github/ytlog/artdcevm/internal/NativeScopeBridge";
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
}
struct Executor::Request {
 Executor* owner;JNIEnv* env;JavaVM* vm;void* self;size_t extent;uint64_t nonce;
 Operation operation;void* data;
 Result result=Result::NoCallback;bool claimed=false,in_operation=false,invalid=false;
 const Context* context=nullptr;
};
bool Executor::State(void* self,size_t extent,uint32_t expected)const noexcept{
 uint32_t actual=0;return current_&&current_()==self&&state_.State(self,extent,&actual)&&actual==expected;
}
bool Executor::Prepare(HookEngine& hooks,JNIEnv* env,const thread_suspend_service::Service& state,std::string* error){
 if(active_||preparing_.test_and_set(std::memory_order_acquire))return Fail(error,"Native executor preparation is reentrant or busy");
 struct Unlock{std::atomic_flag& flag;~Unlock(){flag.clear(std::memory_order_release);}} unlock{preparing_};
 if(prepared_.load(std::memory_order_acquire))return Fail(error,"Native executor is already prepared");
 if(!env||env->ExceptionCheck())return Fail(error,"Native executor requires regular Native JNI without pending exception");
 auto current=reinterpret_cast<decltype(current_)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 void* self=current?current():nullptr;size_t extent=0;uint32_t observed=0;
 if(!self||!method_identity::ReadableMappingExtent(self,&extent)||!state.State(self,extent,&observed)||observed!=abi::arm64::android11::kNative)
  return Fail(error,"Native executor initialization lacks actual current Native state");
 JavaVM* vm=nullptr;JNIEnv* same=nullptr;
 if(env->GetJavaVM(&vm)!=JNI_OK||!vm||vm->GetEnv(reinterpret_cast<void**>(&same),JNI_VERSION_1_6)!=JNI_OK||same!=env)
  return Fail(error,"Native executor initialization VM/environment differs");
 jclass local=env->FindClass(bridge);
 if(!local||env->ExceptionCheck()){if(local)env->DeleteLocalRef(local);return Fail(error,"Native executor carrier lookup failed");}
 jmethodID method=env->GetStaticMethodID(local,"enter","(J)V");
 jclass global=method?static_cast<jclass>(env->NewGlobalRef(local)):nullptr;
 JNINativeMethod native{const_cast<char*>("enter"),const_cast<char*>("(J)V"),reinterpret_cast<void*>(Dispatch)};
 const bool registered=global&&!env->ExceptionCheck()&&env->RegisterNatives(local,&native,1)==JNI_OK;
 env->DeleteLocalRef(local);
 if(!registered||env->ExceptionCheck()){
  if(global)env->DeleteGlobalRef(global);return Fail(error,"Native executor carrier registration failed");
 }
 if(current()!=self||!state.State(self,extent,&observed)||observed!=abi::arm64::android11::kNative){env->DeleteGlobalRef(global);return Fail(error,"Native executor preparation changed actual owner state");}
 state_=state;current_=current;vm_=vm;carrier_=global;method_=method;
 prepared_.store(true,std::memory_order_release);if(error)error->clear();return true;
}
void JNICALL Executor::Dispatch(JNIEnv* env,jclass carrier,jlong nonce) noexcept {
 auto* request=active_;
 if(!request)return;
 if(request->claimed){request->invalid=true;request->result=Result::Reentrant;return;}
 request->claimed=true;
 auto& owner=*request->owner;
 if(env!=request->env||nonce<=0||uint64_t(nonce)!=request->nonce||!owner.State(request->self,request->extent,abi::arm64::android11::kNative)){
  request->result=Result::ContextMismatch;return;
 }
 // All public JNI preparation precedes the operation; public JNI itself can
 // temporarily enter Runnable. Check actual Native state again afterward.
 JavaVM* vm=nullptr;JNIEnv* same=nullptr;
 if(env->ExceptionCheck()||env->GetJavaVM(&vm)!=JNI_OK||vm!=request->vm||
    vm->GetEnv(reinterpret_cast<void**>(&same),JNI_VERSION_1_6)!=JNI_OK||same!=env||
    !env->IsSameObject(carrier,owner.carrier_)||env->ExceptionCheck()){
  request->result=Result::ContextMismatch;return;
 }
 if(!owner.State(request->self,request->extent,abi::arm64::android11::kNative)){request->result=Result::StateMismatch;return;}
 Context context(request->self,env,vm,request->extent,request->nonce);
 request->context=&context;request->in_operation=true;
 Result result=Result::OperationThrew;
 try{result=request->operation(context,request->data)?Result::Success:Result::OperationRejected;}
 catch(...){}
 if(!request->invalid)request->result=result;
 request->in_operation=false;request->context=nullptr;
 if(!owner.State(request->self,request->extent,abi::arm64::android11::kNative))request->result=Result::StateMismatch;
}
bool Executor::Active(const Context* context) noexcept {
 const auto* request=active_;
 return request&&context&&request->in_operation&&request->context==context&&request->nonce==context->nonce_&&
   request->self==context->self_&&request->env==context->env_&&request->vm==context->vm_&&
   request->owner->State(request->self,request->extent,abi::arm64::android11::kNative);
}
bool Context::Active()const noexcept{return Executor::Active(this);}
bool Executor::Identity(const Context& context,void*) noexcept{return context.Active();}
Result Executor::Invoke(const runnable_jni::Context& context,Operation operation,void* data){
 if(!prepared_.load(std::memory_order_acquire))return Result::Unavailable;
 if(active_)return Result::Reentrant;
 auto* env=context.env;
 if(!env||!operation||!context.self||context.vm!=vm_)return Result::InvalidRequest;
 if(env->ExceptionCheck())return Result::PendingException;
 JavaVM* vm=nullptr;JNIEnv* same=nullptr;
 if(env->GetJavaVM(&vm)!=JNI_OK||vm!=vm_||vm->GetEnv(reinterpret_cast<void**>(&same),JNI_VERSION_1_6)!=JNI_OK||same!=env)return Result::ContextMismatch;
 size_t extent=0;
 if(!method_identity::ReadableMappingExtent(context.self,&extent)||!State(context.self,extent,state_.RunnableState()))return Result::StateMismatch;
 const uint64_t nonce=NextNonce();
 if(!nonce||nonce>uint64_t(std::numeric_limits<jlong>::max()))return Result::Unavailable;
 Request request{this,env,vm,context.self,extent,nonce,operation,data};
 struct Reset{Request*& slot;~Reset(){slot=nullptr;}} reset{active_};active_=&request;
 jvalue argument{};argument.j=static_cast<jlong>(nonce);
 try{env->CallStaticVoidMethodA(carrier_,method_,&argument);}
 catch(...){return Result::OperationThrew;}
 // Java exceptions remain pending; never clear or replace an original failure.
 if(env->ExceptionCheck())return Result::JniException;
 if(!State(context.self,extent,state_.RunnableState()))return Result::StateMismatch;
 return request.result;
}
Result Executor::Calibrate(const runnable_jni::Context& context){
 const auto result=Invoke(context,Identity,nullptr);
 if(result==Result::Success)calibrated_.store(true,std::memory_order_release);
 return result;
}
Result Executor::WithNative(const runnable_jni::Context& context,Operation operation,void* data){
 if(!Ready())return Result::Unavailable;
 return Invoke(context,operation,data);
}
}
