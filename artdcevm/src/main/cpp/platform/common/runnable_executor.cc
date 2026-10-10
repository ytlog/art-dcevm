#include "platform/common/runnable_executor.h"
#include "platform/common/runnable_global_ref_bridge.h"
#include "platform/jni_access/capability_selection.h"
#include "platform/abi/arm64/android15/jni_current_thread.h"
#include "platform/abi/arm64/common/suspend_all.h"
#include <atomic>
#include <mutex>
namespace artdcevm::runnable_executor {
namespace {
runnable_jni::Bridge reflected;
runnable_jni::GlobalRefBridge globals;
abi::arm64::android11::CurrentThread current=nullptr;
abi::arm64::android11::CurrentPeer const_peer=nullptr;
abi::arm64::android15::CurrentPeer mutable_peer=nullptr;
abi::arm64::android11::DecodeJObject legacy_decode=nullptr;
runnable_jni::GlobalDecoder global_decode;
jni_access::RuntimeCandidate candidate{};
jobject bootstrap_method=nullptr;
bool global_bound=false,reflected_bound=false,attempted=false;
std::atomic<bool> ready{false};
std::mutex initialization;
thread_local bool executing=false;
thread_local const char* last_route="uncalibrated";
void* ReflectedProxy(const abi::arm64::android_common::RunnableJniScope& scope,jobject carrier){
  return reflected.OnFromReflectedMethod(scope,carrier);
}
jobject GlobalProxy(void* vm,void* self,abi::arm64::android11::ReleaseObjectPointer object){
  return globals.OnAddGlobalRef(vm,self,object);
}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
bool Decode(const runnable_jni::Context& context,jobject strong,void** output){
  if(!output || !context.self || !context.env || current()!=context.self ||
     context.env->ExceptionCheck() || context.env->GetObjectRefType(strong)!=JNIGlobalRefType)return false;
  if(candidate.decoder==jni_access::DecoderModel::kGlobalOnly)
    return global_decode.Decode(context,strong,output);
  void* result=reinterpret_cast<void*>(legacy_decode(context.self,strong).reference);
  if(!result)return false;
  *output=result;return true;
}
struct Request {JNIEnv* env;JavaVM* vm;void* self;jobject thread;
                runnable_jni::Operation operation;void* data;};
bool Dispatch(const runnable_jni::Context& context,void* data){
  auto& request=*static_cast<Request*>(data);
  if(context.env!=request.env || context.vm!=request.vm || context.self!=request.self ||
     current()!=context.self)return false;
  void* thread=nullptr;if(!Decode(context,request.thread,&thread))return false;
  // No managed dereference or safepoint between borrowed peer identities.
  void* peer=const_peer?const_peer(context.self):mutable_peer(context.self);
  if(!thread || thread!=peer)return false;
  return request.operation(context,request.data);
}
bool Identity(const runnable_jni::Context&,void*){return true;}
runnable_jni::Result Invoke(JNIEnv* env,jobject strong,runnable_jni::Operation operation,void* data){
  using Result=runnable_jni::Result;
  if(!env || !strong || !operation)return Result::kInvalidRequest;
  if(executing)return Result::kReentrant;
  if(env->ExceptionCheck())return Result::kPendingException;
  if(env->GetObjectRefType(strong)!=JNIGlobalRefType)return Result::kInvalidRequest;
  struct Guard{Guard(){executing=true;}~Guard(){executing=false;}} guard;
  JavaVM* vm=nullptr;if(env->GetJavaVM(&vm)!=JNI_OK || !vm)return Result::kContextMismatch;
  auto type=env->FindClass("java/lang/Thread");
  auto method=type?env->GetStaticMethodID(type,"currentThread","()Ljava/lang/Thread;"):nullptr;
  auto local=method?env->CallStaticObjectMethod(type,method):nullptr;
  auto thread=local?env->NewGlobalRef(local):nullptr;
  if(local)env->DeleteLocalRef(local);
  if(type)env->DeleteLocalRef(type);
  struct Root{JNIEnv* env;jobject value;~Root(){if(value)env->DeleteGlobalRef(value);}} root{env,thread};
  if(env->ExceptionCheck())return Result::kJniException;
  if(!thread || !current())return Result::kContextMismatch;
  Request request{env,vm,current(),thread,operation,data};
  auto result=global_bound?globals.Invoke(env,strong,Dispatch,&request):Result::kUnavailable;
  last_route="new-global-ref";
  if((result==Result::kLeafNotReached || result==Result::kUnavailable) && reflected_bound){
    // Immutable boot Method carrier is never the application object being
    // evolved. ART may safely encode its jmethodID after operation returns.
    last_route="from-reflected-method";
    result=reflected.Invoke(env,bootstrap_method,Dispatch,&request);
  }
  return result;
}
}
bool Initialize(HookEngine& hooks,JNIEnv* env,std::string* error){
  std::lock_guard<std::mutex> guard(initialization);
  if(ready.load(std::memory_order_acquire)){if(error)error->clear();return true;}
  if(attempted)return Fail(error,"Runnable executor initialization previously failed; restart process");
  if(!env || env->ExceptionCheck())return Fail(error,"Runnable executor requires Native JNI without pending exception");
  attempted=true;
  if(!hooks.Initialize(error))return false;
  auto resolver=[](void* data,const char* name)->void*{return static_cast<HookEngine*>(data)->FindSymbol("libart.so",name);};
  jni_access::RuntimeCandidate selected{};
  if(!jni_access::SelectRuntimeCandidate(resolver,&hooks,&selected,error))return false;
  auto get_current=reinterpret_cast<decltype(current)>(resolver(&hooks,abi::arm64::android11::kCurrentThread));
  auto peer=resolver(&hooks,selected.peer==jni_access::PeerModel::kConst?
      abi::arm64::android11::kCurrentPeer:abi::arm64::android15::kCurrentPeer);
  auto decoder=resolver(&hooks,selected.decoder==jni_access::DecoderModel::kLegacyAny?
      abi::arm64::android11::kDecodeJObject:runnable_jni::GlobalDecoder::SymbolName());
  auto global_target=resolver(&hooks,runnable_jni::GlobalRefBridge::SymbolName());
  auto reflected_target=resolver(&hooks,runnable_jni::Bridge::SymbolName());
  using namespace abi::arm64::android_common;
  auto suspend=reinterpret_cast<SuspendConstruct>(resolver(&hooks,kSuspendConstruct));
  auto resume=reinterpret_cast<SuspendDestroy>(resolver(&hooks,kSuspendDestroy));
  if(!get_current || !peer || !decoder || !suspend || !resume || (!global_target && !reflected_target))
    return Fail(error,"Missing named Runnable executor functions");
  auto object=env->FindClass("java/lang/Object");
  auto method=object?env->GetMethodID(object,"toString","()Ljava/lang/String;"):nullptr;
  auto local=method?env->ToReflectedMethod(object,method,JNI_FALSE):nullptr;
  bootstrap_method=local?env->NewGlobalRef(local):nullptr;
  if(local)env->DeleteLocalRef(local);if(object)env->DeleteLocalRef(object);
  if(!bootstrap_method || env->ExceptionCheck())return Fail(error,"Owned immutable JNI Method carrier unavailable");
  SuspendAll storage{};suspend(&storage,"artdcevm Runnable executor",false);
  struct Resume{SuspendDestroy fn;void* data;~Resume(){fn(data);}} release{resume,&storage};
  void* original=nullptr;
  if(global_target){
    auto token=hooks.Install(global_target,reinterpret_cast<void*>(GlobalProxy),&original,error);
    global_bound=token && runnable_jni::GlobalRefBridge::Bind(original,reinterpret_cast<void*>(get_current),&globals);
    if(!global_bound)return Fail(error,"Runnable global bridge hook could not bind; restart process");
  }
  if(reflected_target){
    original=nullptr;
    auto token=hooks.Install(reflected_target,reinterpret_cast<void*>(ReflectedProxy),&original,error);
    reflected_bound=token && runnable_jni::Bridge::Bind(original,&reflected);
    if(!reflected_bound)return Fail(error,"Runnable reflected bridge hook could not bind; restart process");
  }
  current=get_current;candidate=selected;
  if(selected.peer==jni_access::PeerModel::kConst)const_peer=reinterpret_cast<decltype(const_peer)>(peer);
  else mutable_peer=reinterpret_cast<decltype(mutable_peer)>(peer);
  if(selected.decoder==jni_access::DecoderModel::kLegacyAny)legacy_decode=reinterpret_cast<decltype(legacy_decode)>(decoder);
  else if(!runnable_jni::GlobalDecoder::Bind(runnable_jni::GlobalDecoder::Representation::kMatchedReleaseNoPoison,decoder,&global_decode))
    return Fail(error,"Runnable global decoder candidate unavailable");
  // Resume before entering any public JNI scope or running an owned operation.
  release.fn(release.data);release.fn=[](void*){};
  auto result=Invoke(env,bootstrap_method,Identity,nullptr);
  if(result!=runnable_jni::Result::kSuccess)return Fail(error,"Real JNI dispatch/current-thread identity calibration failed");
  ready.store(true,std::memory_order_release);
  if(error)error->clear();return true;
}
bool Available(){return ready.load(std::memory_order_acquire);}
runnable_jni::Result With(JNIEnv* env,jobject strong,runnable_jni::Operation operation,void* data){
  if(!Available())return runnable_jni::Result::kUnavailable;
  return Invoke(env,strong,operation,data);
}
bool DecodeStrongGlobal(const runnable_jni::Context& context,jobject strong,void** output){
  return executing && Available() && Decode(context,strong,output);
}
const char* Route(){return last_route;}
}
