// Real host JVM registration/call/exception test. ART Thread-state and mapping
// evidence are independent boundary doubles, not a host JVM ART calibration.
#include "platform/common/native_scope_executor.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
using namespace native_scope_executor;
namespace {
thread_local uint32_t flags=92u<<16;
Executor executor;int callbacks=0,operations=0;
void (*registered)(JNIEnv*,jclass,jlong)=nullptr;
const JNINativeInterface_* original=nullptr;
JNINativeInterface_ routed{};
void* Current(){return &flags;}
void Stop(void*,const char*,bool){std::abort();}void Resume(void*){std::abort();}
struct Hooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* Install(void*,void*,void**,std::string*)override{std::abort();}
 bool Remove(void*,std::string*)override{std::abort();}
 void* FindSymbol(const char*,const char* name)override{return !std::strcmp(name,abi::arm64::android11::kCurrentThread)?reinterpret_cast<void*>(Current):nullptr;}
};
void JNICALL Entry(JNIEnv* env,jclass cls,jlong nonce){
 // Only the fixture state reader is doubled. The JVM really dispatches this
 // ordinary registered JNI function with its own native/local-frame lifetime.
 ++callbacks;flags=92u<<16;registered(env,cls,nonce);flags=67u<<16;
}
jint JNICALL Register(JNIEnv* env,jclass cls,const JNINativeMethod* methods,jint count){
 assert(count==1&&!std::strcmp(methods[0].name,"enter")&&!std::strcmp(methods[0].signature,"(J)V"));
 registered=reinterpret_cast<decltype(registered)>(methods[0].fnPtr);
 JNINativeMethod entry{const_cast<char*>("enter"),const_cast<char*>("(J)V"),reinterpret_cast<void*>(Entry)};
 return original->RegisterNatives(env,cls,&entry,1);
}
bool Work(const Context& context,void*){
 assert(context.Active()&&context.Self()==&flags&&context.Extent()==sizeof(flags));
 ++operations;return true;
}
}
namespace artdcevm::method_identity {
bool ReadableMappingExtent(const void* source,size_t* extent){if(source!=&flags)return false;*extent=sizeof(flags);return true;}
}
extern "C" JNIEXPORT jboolean JNICALL Java_NativeScopeExecutorHostProbe_run(JNIEnv* env,jclass){
 original=env->functions;routed=*original;routed.RegisterNatives=Register;env->functions=&routed;
 thread_suspend_service::Service state;
 assert(thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kLegacy16,[](const char* name)->void*{
  using namespace abi::arm64::android_common; using namespace ::artdcevm::abi::common;
  if(!std::strcmp(name,kSuspendConstruct))return reinterpret_cast<void*>(Stop);
  if(!std::strcmp(name,kSuspendDestroy))return reinterpret_cast<void*>(Resume);return nullptr;
 },&state));
 Hooks hooks;std::string error;flags=92u<<16;
 assert(executor.Prepare(hooks,env,state,&error));JavaVM* vm=nullptr;assert(env->GetJavaVM(&vm)==JNI_OK);
 flags=67u<<16;runnable_jni::Context context{&flags,env,vm};
 assert(executor.Calibrate(context)==Result::Success&&executor.Ready());
 assert(executor.WithNative(context,Work,nullptr)==Result::Success);
 assert(callbacks==2&&operations==1&&flags==(67u<<16)&&!env->ExceptionCheck());
 env->functions=original;std::cout<<"Real host JVM ordinary registered JNI entry/return and same environment PASS\n";return JNI_TRUE;
}
extern "C" JNIEXPORT void JNICALL Java_NativeScopeExecutorHostProbe_pending(JNIEnv* env,jclass){
 flags=67u<<16;JavaVM* vm=nullptr;assert(env->GetJavaVM(&vm)==JNI_OK);
 auto klass=env->FindClass("java/lang/IllegalStateException");assert(klass);
 assert(env->ThrowNew(klass,"original exception survives executor")==JNI_OK);env->DeleteLocalRef(klass);
 const auto before=callbacks;runnable_jni::Context context{&flags,env,vm};
 assert(executor.WithNative(context,Work,nullptr)==Result::PendingException&&env->ExceptionCheck()&&callbacks==before);
 // Return with the exact original pending exception, handled only by Java.
}
