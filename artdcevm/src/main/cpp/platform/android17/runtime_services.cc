#include "platform/android17/runtime_services.h"
#include "platform/android17/runtime_receiver_probe.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include <jvmti.h>
#include <mutex>
namespace artdcevm::android17 {
namespace {
// Fixed b753cf97923c instrumentation.h: scoped enum ListenerType uses the
// default int underlying type, never the older bool trace argument.
enum class ListenerType {kRegularListener,kFastTraceListener,kSlowTraceListener};
static_assert(sizeof(ListenerType)==4);
constexpr char kListener[]="_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEjNS1_12ListenerTypeE";
using AddListener=void (*)(void*,void*,uint32_t,ListenerType);
AddListener original=nullptr;
struct Observation {void* instrumentation=nullptr;bool conflict=false;};
thread_local Observation* active=nullptr;
std::mutex installation;
bool attempted=false,installed=false;
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
void ObserveListener(void* receiver,void* listener,uint32_t events,ListenerType trace) {
  if(active) {
    if(!receiver||(active->instrumentation&&active->instrumentation!=receiver))active->conflict=true;
    else active->instrumentation=receiver;
  }
  original(receiver,listener,events,trace);
}
bool Install(HookEngine& hooks,std::string* error) {
  std::lock_guard<std::mutex> lock(installation);
  if(installed)return true;
  // Missing candidates do not poison a later independent family selection.
  auto target=hooks.FindSymbol("libart.so",kListener);
  using namespace abi::arm64::android_common;
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!target||!construct||!destroy)return Fail(error,"Missing named enum-listener Runtime service capability");
  if(attempted)return Fail(error,"Instrumentation observation installation failed; restart process");
  attempted=true;
  SuspendAll storage{};
  construct(&storage,"artdcevm Instrumentation receiver observation",false);
  void* trampoline=nullptr;
  auto token=hooks.Install(target,reinterpret_cast<void*>(ObserveListener),&trampoline,error);
  original=reinterpret_cast<AddListener>(trampoline);
  installed=token&&original;
  destroy(&storage);
  return installed||Fail(error,"Instrumentation observation hook unavailable; restart process");
}
void MethodEntry(jvmtiEnv*,JNIEnv*,jthread,jmethodID) {}
struct OwnedEnvironment {
  jvmtiEnv* env=nullptr;
  bool enabled=false;
  ~OwnedEnvironment(){
    if(!env)return;
    if(enabled)env->SetEventNotificationMode(JVMTI_DISABLE,JVMTI_EVENT_METHOD_ENTRY,nullptr);
    env->DisposeEnvironment();
  }
};
struct ReadRequest {
  void* runtime;
  android17::RuntimeReceivers observed;
  runtime_services::View view;
};
bool ReadServices(const runnable_jni::Context&,void* opaque) {
  auto& request=*static_cast<ReadRequest*>(opaque);
  size_t extent=0;
  if(!method_identity::ReadableMappingExtent(request.runtime,&extent))return false;
  if(!runtime_services::Read(runtime_services::Declaration::kAndroid15To17,request.runtime,
      extent,&request.view))return false;
  return request.view.class_linker==request.observed.class_linker&&
      request.view.jni_id_manager==request.observed.jni_id_manager&&
      request.view.thread_list==request.observed.thread_list&&
      request.view.java_vm==request.observed.java_vm;
}
}
bool CalibrateRuntimeServices(HookEngine& hooks,JNIEnv* env,RuntimeServices* output,
                              std::string* error) {
  if(error)error->clear();
  if(!env||!output||active||env->ExceptionCheck())return Fail(error,"Invalid Runtime service calibration caller");
  if(!hooks.Initialize(error)||!runnable_executor::Initialize(hooks,env,error)||!Install(hooks,error))return false;
  android17::RuntimeReceivers observed;
  if(!android17::CalibrateRuntimeReceivers(hooks,env,&observed,error))return false;
  // Fixed OpenjdkJvmTi.cc GetEnvHandler always CreateArtJvmTiEnv. This is an
  // owned, distinct temporary environment; disposal never affects the agent env.
  OwnedEnvironment owned;
  if(observed.java_vm->GetEnv(reinterpret_cast<void**>(&owned.env),JVMTI_VERSION_1_2)!=JNI_OK||!owned.env)
    return Fail(error,"Independent JVMTI environment unavailable");
  jvmtiCapabilities capabilities{};capabilities.can_generate_method_entry_events=1;
  jvmtiEventCallbacks callbacks{};callbacks.MethodEntry=MethodEntry;
  if(owned.env->AddCapabilities(&capabilities)!=JVMTI_ERROR_NONE||
     owned.env->SetEventCallbacks(&callbacks,sizeof(callbacks))!=JVMTI_ERROR_NONE)
    return Fail(error,"Independent method-entry observation capability unavailable");
  Observation observation;
  active=&observation;
  auto enabled=owned.env->SetEventNotificationMode(JVMTI_ENABLE,JVMTI_EVENT_METHOD_ENTRY,nullptr);
  active=nullptr;
  owned.enabled=enabled==JVMTI_ERROR_NONE;
  if(!owned.enabled||observation.conflict||!observation.instrumentation)
    return Fail(error,"Public JVMTI did not establish Instrumentation receiver");
  auto singleton=static_cast<void**>(hooks.FindSymbol("libart.so","_ZN3art7Runtime9instance_E"));
  if(!singleton||!*singleton)return Fail(error,"Named Runtime instance unavailable");
  jclass klass=env->FindClass("java/lang/Thread");
  jmethodID method=klass?env->GetStaticMethodID(klass,"currentThread","()Ljava/lang/Thread;"):nullptr;
  jobject thread=method?env->CallStaticObjectMethod(klass,method):nullptr;
  jobject root=thread&&!env->ExceptionCheck()?env->NewGlobalRef(thread):nullptr;
  if(thread)env->DeleteLocalRef(thread);
  if(klass)env->DeleteLocalRef(klass);
  if(!root||env->ExceptionCheck())return Fail(error,"Owned Runtime receiver calibration carrier unavailable");
  ReadRequest request{*singleton,observed,{}};
  auto result=runnable_executor::With(env,root,ReadServices,&request);
  env->DeleteGlobalRef(root);
  if(result!=runnable_jni::Result::kSuccess)return Fail(error,"Runtime source declaration disagrees with observed service receivers");
  request.view.heap=nullptr;request.view.jit=nullptr;request.view.jit_code_cache=nullptr;
  *output={request.runtime,observation.instrumentation,request.view};
  return true;
}
}
