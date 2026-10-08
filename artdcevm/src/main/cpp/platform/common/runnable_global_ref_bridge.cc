#include "platform/common/runnable_global_ref_bridge.h"
namespace artdcevm::runnable_jni {
struct GlobalRefBridge::Request {
  const GlobalRefBridge* bridge;JNIEnv* env;jobject input;JavaVM* vm;void* self;
  Operation operation;void* data;bool claimed=false;Result result=Result::kLeafNotReached;
};
thread_local GlobalRefBridge::Request* GlobalRefBridge::active_=nullptr;
bool GlobalRefBridge::Bind(void* original,void* current_thread,GlobalRefBridge* output){
  if(!original||!current_thread||!output)return false;
  GlobalRefBridge candidate;
  candidate.original_=reinterpret_cast<Leaf>(original);
  candidate.current_=reinterpret_cast<decltype(current_)>(current_thread);
  *output=candidate;return true;
}
Result GlobalRefBridge::Invoke(JNIEnv* env,jobject strong_global,Operation operation,void* data) const {
  if(!original_||!current_)return Result::kUnavailable;
  if(!env||!strong_global||!operation)return Result::kInvalidRequest;
  if(active_)return Result::kReentrant;
  if(env->ExceptionCheck())return Result::kPendingException;
  if(env->GetObjectRefType(strong_global)!=JNIGlobalRefType)return Result::kInvalidRequest;
  if(env->ExceptionCheck())return Result::kJniException;
  JavaVM* vm=nullptr;void* self=current_();
  if(!self||env->GetJavaVM(&vm)!=JNI_OK||!vm)return Result::kContextMismatch;
  if(env->ExceptionCheck())return Result::kJniException;
  Request request{this,env,strong_global,vm,self,operation,data};
  struct Reset {Request*& slot;~Reset(){slot=nullptr;}} reset{active_};
  active_=&request;
  jobject duplicate=env->NewGlobalRef(strong_global);
  struct Root {JNIEnv* env;jobject value;~Root(){if(value)env->DeleteGlobalRef(value);}} root{env,duplicate};
  if(env->ExceptionCheck())return Result::kJniException;
  if(!duplicate)return Result::kOriginalRejected;
  return request.result;
}
jobject GlobalRefBridge::OnAddGlobalRef(void* vm,void* self,
                                      abi::arm64::android11::ReleaseObjectPointer object) const {
  if(!original_)return nullptr;
  Request* request=active_;
  // Claim before executing original: recursive ordinary JNI calls must never
  // dispatch twice. Public JNI identity comparison follows original root creation.
  bool selected=request&&request->bridge==this&&!request->claimed;
  if(selected)request->claimed=true;
  jobject root=original_(vm,self,object);
  if(!selected)return root;
  if(vm!=request->vm||self!=request->self||!self||current_()!=self){
    request->result=Result::kContextMismatch;return root;
  }
  JNIEnv* env=request->env;
  if(env->ExceptionCheck()){request->result=Result::kJniException;return root;}
  if(!root){request->result=Result::kOriginalRejected;return root;}
  if(!env->IsSameObject(root,request->input)){
    request->result=env->ExceptionCheck()?Result::kJniException:Result::kContextMismatch;return root;
  }
  if(env->ExceptionCheck()){request->result=Result::kJniException;return root;}
  try{
    request->result=request->operation({self,env,vm},request->data)
        ?Result::kSuccess:Result::kOperationRejected;
  }catch(...){request->result=Result::kOperationThrew;}
  return root;
}
}
