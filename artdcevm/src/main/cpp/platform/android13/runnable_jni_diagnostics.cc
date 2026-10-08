#include "platform/android13/runnable_jni_diagnostics.h"
#ifdef ARTDCEVM_TESTING
#include "platform/android13/evolution_runtime.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/native_object_access_abi.h"
#include "platform/android13/thread_suspend_scope.h"
#include "platform/common/runnable_jni_bridge.h"
#include "hooks/shadow_hook_engine.h"
#include <cstring>
#include <mutex>
#include <cstdlib>
namespace artdcevm::android13 {
namespace {
using namespace runnable_jni;
Bridge bridge;
void* hook_token=nullptr;
std::mutex diagnostic_mutex;
uint32_t State(void* self) {
  auto* flags=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(self)+native_access_abi::kThreadFlagsOffset);
  return __atomic_load_n(flags,__ATOMIC_RELAXED)>>native_access_abi::kStateShift;
}
void* Proxy(const abi::arm64::android11::RunnableJniScope& scope,jobject carrier) {
  return bridge.OnFromReflectedMethod(scope,carrier);
}
struct Probe {JNIEnv* env;void* self;void* vm;jobject carrier; jobject local; jobject global;int bits=0;int hits=0;};
bool Observe(const Context& context,void* data) {
  auto& probe=*static_cast<Probe*>(data);++probe.hits;
  if(State(context.self)==native_access_abi::kRunnable)probe.bits|=2;
  if(context.self==probe.self&&context.env==probe.env&&context.vm==probe.vm)probe.bits|=4;
  if(context.env->GetObjectRefType(probe.global)==JNIGlobalRefType&&
     context.env->IsSameObject(probe.global,probe.local))probe.bits|=8;
  // Both borrowed mirrors are consumed inside the real Runnable scope; none escape.
  void* mirror=DecodeRunnableObject(context.self,probe.global);
  if(mirror&&mirror==DecodeRunnableObject(context.self,probe.local))probe.bits|=16;
  if(bridge.Invoke(context.env,probe.carrier,Observe,data)==Result::kReentrant)probe.bits|=32;
  return probe.bits==62&&!context.env->ExceptionCheck();
}
}
int DiagnosticRunnableJni(JNIEnv* env,jobject carrier,jobject fixture) {
  if(!EvolutionRuntimeAvailable()||!env||!carrier||!fixture||env->ExceptionCheck())return -1;
  std::unique_lock<std::mutex> guard(diagnostic_mutex);
  void* self=nullptr;
  std::memcpy(&self,reinterpret_cast<unsigned char*>(env)+offsetof(native_access_abi::EnvironmentPrefix,self),sizeof(self));
  if(!self||State(self)!=native_access_abi::kNative)return -2;
  if(!hook_token) {
    static ShadowHookEngine hooks;std::string error;
    if(!hooks.Initialize(&error))return -3;
    void* target=hooks.FindSymbol("libart.so",Bridge::SymbolName());
    if(!target)return -4;
    // Publish bridge/trampoline while other mutators are stopped. Keep the hook
    // and service for this diagnostic process lifetime, including passthroughs.
    NativeSuspendScope native(self);if(!native.Supported())return -5;
    OwnedSuspendAll stopped("Calibrate Runnable JNI diagnostic");
    void* original=nullptr;
    hook_token=hooks.Install(target,reinterpret_cast<void*>(Proxy),&original,&error);
    if(!hook_token)return -6;
    if(!Bridge::Bind(original,&bridge))std::abort();
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
  Probe probe{env,self,vm,carrier,fixture,global};
  for(int i=0;i<128;++i) {
    probe.bits=0;
    if(State(self)==native_access_abi::kNative)probe.bits|=1;
    // Callback uses its own mask; preserve the outside-state observation below.
    int native_before=probe.bits;probe.bits=0;
    if(bridge.Invoke(env,carrier,Observe,&probe)!=Result::kSuccess)return -10;
    probe.bits|=native_before;
    if(State(self)==native_access_abi::kNative)probe.bits|=64;
    if(env->FromReflectedMethod(carrier)==expected&&!env->ExceptionCheck())probe.bits|=128;
    if(probe.bits!=255)return probe.bits;
  }
  return probe.hits==128?255:-11;
}
}
#endif
