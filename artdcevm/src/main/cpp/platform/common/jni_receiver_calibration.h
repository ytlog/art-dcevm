#pragma once
#include "api/hook_engine.h"
#include "platform/abi/common/jni_current_thread.h"
#include "platform/common/borrowed_jni_callback.h"
#include "platform/common/jni_receiver_metadata.h"
namespace artdcevm::jni_receiver_calibration {
using CallbackResult=borrowed_jni_callback::Result;
enum class Result {kSuccess,kUnavailable,kInvalidRequest,kPendingException,
                   kVmUnavailable,kCurrentUnavailable,kReceiverMismatch,
                   kCallbackRejected,kJniException};
struct Report {Result result;CallbackResult callback;size_t original_callback_hits=0,matched_owner_prefixes=0;};
// PointerWidth describes the executing JNI ABI, never a remote scope. The live
// environment belongs to the current owning thread; caller supplies its mapped
// extent and a rooted reflected Method/Constructor. Named symbol/provider image,
// original hook and trampoline must stay alive for all calls and passthroughs.
template<size_t PointerWidth> class Service {
  static_assert(PointerWidth==sizeof(void*), "calibrate only the executing CPU ABI");
 public:
  static bool Bind(HookEngine& hooks,void* original,Service* output,std::string* error) {
    if(!output || !original) {if(error)*error="missing original JNI leaf";return false;}
    auto current=reinterpret_cast<abi::common::CurrentThread>(
        hooks.FindSymbol("libart.so",abi::common::kCurrentThread));
    auto target=hooks.FindSymbol("libart.so",abi::common::kFromReflectedMethod);
    Service candidate;
    if(!current || !target || !borrowed_jni_callback::Bridge::Bind(original,&candidate.bridge_)) {
      if(error)*error="missing named current Thread or reflected-method leaf";
      return false;
    }
    candidate.current_=current;*output=candidate;return true;
  }
  Report Calibrate(JNIEnv* env,jobject carrier,size_t mapped_extent,borrowed_jni_callback::Operation operation=nullptr,void* data=nullptr) const {
    if(!current_)return {Result::kUnavailable,CallbackResult::kUnavailable};
    if(!env || !carrier)return {Result::kInvalidRequest,CallbackResult::kInvalidRequest};
    if(env->ExceptionCheck())return {Result::kPendingException,CallbackResult::kPendingException};
    JavaVM* vm=nullptr;
    const jint status=env->GetJavaVM(&vm);
    if(env->ExceptionCheck())return {Result::kJniException,CallbackResult::kJniException};
    if(status!=JNI_OK || !vm)return {Result::kVmUnavailable,CallbackResult::kUnavailable};
    void* self=current_();
    if(!self)return {Result::kCurrentUnavailable,CallbackResult::kUnavailable};
    jni_receiver_metadata::Receiver prefix{};
    if(!jni_receiver_metadata::ReadMatchedPrefix(PointerWidth,env,mapped_extent,
          reinterpret_cast<uintptr_t>(self),reinterpret_cast<uintptr_t>(vm),&prefix))
      return {Result::kReceiverMismatch,CallbackResult::kContextMismatch};
    Request request{this,env,vm,self,mapped_extent,operation,data};
    const auto result=bridge_.Invoke(env,carrier,Observe,&request);
    if(env->ExceptionCheck())return {Result::kJniException,result,request.callback_hits,request.owner_prefixes};
    // Check the independent named owner again after ART restored native scope.
    const bool restored=current_()==self;
    return {result==CallbackResult::kSuccess && restored
        ? Result::kSuccess : Result::kCallbackRejected,result,request.callback_hits,request.owner_prefixes};
  }
  void* OnFromReflectedMethod(const abi::common::RunnableJniScope& scope,
                             jobject carrier) const {
    return bridge_.OnFromReflectedMethod(scope,carrier);
  }
 private:
  struct Request {const Service* service;JNIEnv* env;JavaVM* vm;void* self;size_t extent;borrowed_jni_callback::Operation operation;void* data;size_t callback_hits=0,owner_prefixes=1;};
  static bool Observe(const borrowed_jni_callback::Context& context,void* data) {
    auto& request=*static_cast<Request*>(data);
    ++request.callback_hits;
    if(context.env!=request.env || context.self!=request.self || context.vm!=request.vm ||
        request.service->current_()!=request.self || context.env->ExceptionCheck())return false;
    JavaVM* vm=nullptr;
    if(context.env->GetJavaVM(&vm)!=JNI_OK || context.env->ExceptionCheck() || vm!=request.vm)
      return false;
    jni_receiver_metadata::Receiver prefix{};
    const bool matched=jni_receiver_metadata::ReadMatchedPrefix(PointerWidth,context.env,request.extent,
        reinterpret_cast<uintptr_t>(context.self),reinterpret_cast<uintptr_t>(context.vm),&prefix);
    if(matched)++request.owner_prefixes;
    return matched && (!request.operation || request.operation(context,request.data));
  }
  abi::common::CurrentThread current_=nullptr;
  borrowed_jni_callback::Bridge bridge_;
};
}
