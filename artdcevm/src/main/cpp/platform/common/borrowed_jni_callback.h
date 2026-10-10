#pragma once
#include "platform/abi/common/jni_access.h"
namespace artdcevm::borrowed_jni_callback {
struct Context {void* self;JNIEnv* env;void* vm;void* original_method=nullptr;abi::common::FromReflectedMethod original_leaf=nullptr;};
using Operation=bool (*)(const Context&,void*);
enum class Result {kSuccess,kUnavailable,kInvalidRequest,kPendingException,kReentrant,
                   kLeafNotReached,kContextMismatch,kOriginalRejected,kOperationRejected,
                   kOperationThrew,kJniException};
// Native pointer/ref return and arguments use the current CPU's C++ ABI, not an
// AArch64 register carrier. Bind only ART's original named leaf trampoline and
// keep it alive. Hook installation/removal and original publication are caller
// duties. No artificial Runnable scope or mutator-lock transition is created.
class Bridge {
 public:
  using Leaf=abi::common::FromReflectedMethod;
  // Only the exact Context object dispatched by this original named callback
  // can borrow its live ART scope. Copying matching fields grants no scope.
  static const abi::common::RunnableJniScope* BorrowOriginalScope(const Context&context)noexcept {
    return active_&&active_->claimed&&original_context_==&context&&original_scope_&&
      original_scope_->self==context.self&&original_scope_->env==context.env&&original_scope_->vm==context.vm?
      original_scope_:nullptr;
  }
  static bool HasActiveInvocation(){return active_!=nullptr;}
  static const char* SymbolName(){return abi::common::kFromReflectedMethod;}
  static bool Bind(void* original,Bridge* output) {
    if(!original || !output)return false;
    Bridge candidate;candidate.original_=reinterpret_cast<Leaf>(original);
    *output=candidate;return true;
  }
  // carrier is an owned live Method/Constructor. Public JNI creates the real
  // Runnable scope; named-leaf bypass returns kLeafNotReached. Preserve pending
  // exceptions, JNI roots and hook lifetimes. Managed pointers must be rooted
  // before a safepoint and cannot escape the callback as borrowed raw pointers.
  Result Invoke(JNIEnv* env,jobject carrier,Operation operation,void* data) const {
    if(!original_)return Result::kUnavailable;
    if(!env || !carrier || !operation)return Result::kInvalidRequest;
    if(active_)return Result::kReentrant;
    if(env->ExceptionCheck())return Result::kPendingException;
    Request request{this,env,carrier,operation,data};
    struct Reset {Request*& slot;~Reset(){slot=nullptr;}} reset{active_};
    active_=&request;
    jmethodID method=env->FromReflectedMethod(carrier);
    if(env->ExceptionCheck())return Result::kJniException;
    if(!method)return Result::kOriginalRejected;
    return request.result;
  }
  void* OnFromReflectedMethod(const abi::common::RunnableJniScope& scope,
                             jobject carrier) const {
    if(!original_)return nullptr;
    Request* request=active_;
    bool selected=request && request->bridge==this && !request->claimed &&
        request->carrier==carrier;
    if(selected)request->claimed=true;
    // Preserve ART's real call/return for both selected and ordinary JNI calls.
    void* method=original_(scope,carrier);
    if(!selected)return method;
    if(!scope.self || scope.env!=request->env || !scope.vm) {
      request->result=Result::kContextMismatch;return method;
    }
    if(scope.env->ExceptionCheck()) {request->result=Result::kJniException;return method;}
    if(!method) {request->result=Result::kOriginalRejected;return method;}
    try {
      Context context{scope.self,scope.env,scope.vm,method,original_};
      if(original_context_||original_scope_){request->result=Result::kReentrant;return method;}
      struct Borrow {~Borrow(){original_context_=nullptr;original_scope_=nullptr;}} borrow;
      original_context_=&context;original_scope_=&scope;
      request->result=request->operation(context,request->data)?Result::kSuccess:Result::kOperationRejected;
    } catch(...) {request->result=Result::kOperationThrew;}
    return method;
  }
 private:
  struct Request {
    const Bridge* bridge;JNIEnv* env;jobject carrier;Operation operation;void* data;
    bool claimed=false;Result result=Result::kLeafNotReached;
  };
  inline static thread_local Request* active_=nullptr;
  inline static thread_local const Context* original_context_=nullptr;
  inline static thread_local const abi::common::RunnableJniScope* original_scope_=nullptr;
  Leaf original_=nullptr;
};
}
