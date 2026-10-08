#include "platform/android13/runnable_jni_diagnostics.h"
#ifdef ARTDCEVM_TESTING
#include "platform/android13/evolution_runtime.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/native_object_access_abi.h"
#include "platform/common/runnable_executor.h"
#include "hooks/shadow_hook_engine.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <cstring>
#include <mutex>
#include <cstdlib>
namespace artdcevm::android13 {
namespace {
using namespace runnable_jni;
bool bound=false;
abi::arm64::android11::CurrentThread current_thread=nullptr;
abi::arm64::android11::CurrentPeer current_peer=nullptr;
std::mutex diagnostic_mutex;
uint32_t State(void* self) {
  auto* flags=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(self)+native_access_abi::kThreadFlagsOffset);
  return __atomic_load_n(flags,__ATOMIC_RELAXED)>>native_access_abi::kStateShift;
}
struct Probe {JNIEnv* env;void* self;void* vm;jobject carrier; jobject local; jobject global;jobject thread_global;int bits=0;int hits=0;};
bool Observe(const Context& context,void* data) {
  auto& probe=*static_cast<Probe*>(data);++probe.hits;
  if(State(context.self)==native_access_abi::kRunnable)probe.bits|=2;
  if(context.self==probe.self&&context.env==probe.env&&context.vm==probe.vm)probe.bits|=4;
  if(context.env->GetObjectRefType(probe.global)==JNIGlobalRefType&&
     context.env->IsSameObject(probe.global,probe.local))probe.bits|=8;
  // Both borrowed mirrors are consumed inside the real Runnable scope; none escape.
  void* mirror=DecodeRunnableObject(context.self,probe.global);
  if(mirror&&mirror==DecodeRunnableObject(context.self,probe.local))probe.bits|=16;
  if(runnable_executor::With(context.env,probe.global,Observe,data)==Result::kReentrant)probe.bits|=32;
  if(current_thread()==context.self)probe.bits|=256;
  void* peer=DecodeRunnableObject(context.self,probe.thread_global);
  if(peer&&peer==current_peer(context.self))probe.bits|=512;
  return probe.bits==830&&!context.env->ExceptionCheck();
}
}
int DiagnosticRunnableJni(JNIEnv* env,jobject carrier,jobject fixture) {
  if(!EvolutionRuntimeAvailable()||!env||!carrier||!fixture||env->ExceptionCheck())return -1;
  std::unique_lock<std::mutex> guard(diagnostic_mutex);
  void* self=nullptr;
  std::memcpy(&self,reinterpret_cast<unsigned char*>(env)+offsetof(native_access_abi::EnvironmentPrefix,self),sizeof(self));
  if(!self||State(self)!=native_access_abi::kNative)return -2;
  if(!bound) {
    static ShadowHookEngine hooks;std::string error;
    if(!runnable_executor::Initialize(hooks,env,&error))return -3;
    auto current=reinterpret_cast<decltype(current_thread)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
    auto peer=reinterpret_cast<decltype(current_peer)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentPeer));
    if(!current||!peer)return -4;
    current_thread=current;current_peer=peer;bound=true;
  }
  guard.unlock(); // Immutable binding; per-thread requests now run concurrently.
  JavaVM* vm=nullptr;if(env->GetJavaVM(&vm)!=JNI_OK||!vm)return -7;
  jobject global=env->NewGlobalRef(fixture);if(!global)return -8;
  struct DeleteGlobal {JNIEnv* env;jobject ref;~DeleteGlobal(){env->DeleteGlobalRef(ref);}} cleanup{env,global};
  jmethodID expected=env->FromReflectedMethod(carrier);if(!expected||env->ExceptionCheck())return -9;
  jclass klass=env->GetObjectClass(fixture);
  if(!klass)return -12;
  jmethodID public_id=env->GetMethodID(klass,"toString","()Ljava/lang/String;");
  env->DeleteLocalRef(klass);
  if(public_id!=expected||env->ExceptionCheck())return -13;
  jclass thread_class=env->FindClass("java/lang/Thread");if(!thread_class)return -14;
  jmethodID current=env->GetStaticMethodID(thread_class,"currentThread","()Ljava/lang/Thread;");
  if(!current){env->DeleteLocalRef(thread_class);return -15;}
  jobject local_thread=env->CallStaticObjectMethod(thread_class,current);
  env->DeleteLocalRef(thread_class);
  if(!local_thread||env->ExceptionCheck())return -16;
  jobject thread_global=env->NewGlobalRef(local_thread);env->DeleteLocalRef(local_thread);
  if(!thread_global)return -17;
  DeleteGlobal thread_cleanup{env,thread_global};
  Probe probe{env,self,vm,carrier,fixture,global,thread_global};
  for(int i=0;i<128;++i) {
    probe.bits=0;
    if(State(self)==native_access_abi::kNative)probe.bits|=1;
    // Callback uses its own mask; preserve the outside-state observation below.
    int native_before=probe.bits;probe.bits=0;
    if(runnable_executor::With(env,global,Observe,&probe)!=Result::kSuccess)return -10;
    probe.bits|=native_before;
    if(State(self)==native_access_abi::kNative)probe.bits|=64;
    if(env->FromReflectedMethod(carrier)==expected&&!env->ExceptionCheck())probe.bits|=128;
    if(probe.bits!=1023)return probe.bits;
  }
  return probe.hits==128?1023:-11;
}
}
#endif
