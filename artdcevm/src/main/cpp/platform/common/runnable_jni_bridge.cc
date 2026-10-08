#include "platform/common/runnable_jni_bridge.h"
namespace artdcevm::runnable_jni {
struct Bridge::Request {
  const Bridge* bridge;JNIEnv* env;jobject carrier;Operation operation;void* data;
  bool claimed=false;Result result=Result::kLeafNotReached;
};
thread_local Bridge::Request* Bridge::active_=nullptr;
bool Bridge::Bind(void* original,Bridge* output) {
  if(!original || !output)return false;
  Bridge candidate;candidate.original_=reinterpret_cast<Leaf>(original);*output=candidate;return true;
}
Result Bridge::Invoke(JNIEnv* env,jobject carrier,Operation operation,void* data) const {
  if(!original_)return Result::kUnavailable;
  if(!env || !carrier || !operation)return Result::kInvalidRequest;
  if(active_)return Result::kReentrant;
  if(env->ExceptionCheck())return Result::kPendingException;
  Request request{this,env,carrier,operation,data};
  // This call is synchronous; restore TLS even if a test JNI implementation or
  // an embedding caller throws a C++ exception. Operations are caught in the leaf.
  struct Reset {Request*& slot;~Reset(){slot=nullptr;}} reset{active_};
  active_=&request;
  jmethodID method=env->FromReflectedMethod(carrier);
  if(env->ExceptionCheck())return Result::kJniException;
  if(!method)return Result::kOriginalRejected;
  return request.result;
}
void* Bridge::OnFromReflectedMethod(const abi::arm64::android_common::RunnableJniScope& scope,
                                   jobject carrier) const {
  if(!original_)return nullptr;
  Request* request=active_;
  bool selected=request && request->bridge==this && !request->claimed && request->carrier==carrier;
  if(selected)request->claimed=true;
  // Preserve original helper execution and return for both dispatched and normal
  // JNI calls. It supplies the native ArtMethod used by ART's later ID encoding.
  void* method=original_(scope,carrier);
  if(!selected)return method;
  if(!scope.self || scope.env!=request->env || !scope.vm) {
    request->result=Result::kContextMismatch;return method;
  }
  if(scope.env->ExceptionCheck()) {request->result=Result::kJniException;return method;}
  if(!method) {request->result=Result::kOriginalRejected;return method;}
  try {
    request->result=request->operation({scope.self,scope.env,scope.vm},request->data)
        ? Result::kSuccess : Result::kOperationRejected;
  } catch(...) {request->result=Result::kOperationThrew;}
  return method;
}
bool GlobalDecoder::Bind(Representation representation,void* symbol,GlobalDecoder* output) {
  if(representation!=Representation::kMatchedReleaseNoPoison || !symbol || !output)return false;
  GlobalDecoder candidate;candidate.decode_=reinterpret_cast<decltype(decode_)>(symbol);*output=candidate;return true;
}
bool GlobalDecoder::Decode(const Context& context,jobject strong_global,void** output) const {
  if(!decode_ || !context.self || !context.env || !context.vm || !output || context.env->ExceptionCheck())return false;
  if(!strong_global){*output=nullptr;return true;}
  if(context.env->GetObjectRefType(strong_global)!=JNIGlobalRefType ||
     context.env->ExceptionCheck())return false;
  auto decoded=decode_(context.self,strong_global);
  if(!decoded.reference || context.env->ExceptionCheck())return false;
  *output=reinterpret_cast<void*>(decoded.reference);return true;
}
}
