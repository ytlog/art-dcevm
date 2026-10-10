#include "platform/common/runtime_service_calibration.h"
#include "platform/abi/arm64/common/instrumentation.h"
#include "platform/common/runtime_receiver_probe.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/runtime_instance_probe.h"
#include "platform/abi/common/suspend_all.h"
#include <jvmti.h>
#include <mutex>
namespace artdcevm::runtime_services {
namespace {
// Fixed source overloads are independent named capabilities. Observers only
// forward the original listener argument; they never synthesize a trace kind.
constexpr char kListener[]="_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEjb";
using AddListener=void (*)(void*,void*,uint32_t,bool);
// Fixed android-11/12.0.0_r1 instrumentation.h: no third argument.
using LegacyAddListener=void (*)(void*,void*,uint32_t);
constexpr char kLegacyListener[]="_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEj";
void* original_storage=nullptr;
struct Observation {void* instrumentation=nullptr;bool conflict=false;};
thread_local Observation* active=nullptr;
std::mutex installation;
bool attempted=false,installed=false;
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
void ObserveReceiver(void* receiver) {
  if(active) {
    if(!receiver||(active->instrumentation&&active->instrumentation!=receiver))active->conflict=true;
    else active->instrumentation=receiver;
  }
}
void ObserveTypedListener(void* receiver,void* listener,uint32_t events,
                          abi::arm64::android_common::ListenerType type) {
  ObserveReceiver(receiver);
  reinterpret_cast<abi::arm64::android_common::TypedAddListener>(original_storage)(
      receiver,listener,events,type);
}
void ObserveListener(void* receiver,void* listener,uint32_t events,bool trace) {
  ObserveReceiver(receiver);
  reinterpret_cast<AddListener>(original_storage)(receiver,listener,events,trace);
}
void ObserveLegacyListener(void* receiver,void* listener,uint32_t events) {
  ObserveReceiver(receiver);
  reinterpret_cast<LegacyAddListener>(original_storage)(receiver,listener,events);
}
bool Install(HookEngine& hooks,std::string* error) {
  std::lock_guard<std::mutex> lock(installation);
  if(installed)return true;
  // Missing candidates do not poison a later independent family selection.
  using namespace abi::arm64::android_common; using namespace ::artdcevm::abi::common;
  auto target=hooks.FindSymbol("libart.so",kTypedAddListener);
  void* observer=reinterpret_cast<void*>(ObserveTypedListener);
  if(!target) {
    target=hooks.FindSymbol("libart.so",kListener);
    observer=reinterpret_cast<void*>(ObserveListener);
  }
  if(!target) {
    target=hooks.FindSymbol("libart.so",kLegacyListener);
    observer=reinterpret_cast<void*>(ObserveLegacyListener);
  }
  if(!target)return Fail(error,"Missing named Instrumentation AddListener capability (ListenerType/bool/legacy)");
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!construct)return Fail(error,"Missing named ScopedSuspendAll constructor for Instrumentation observation");
  if(!destroy)return Fail(error,"Missing named ScopedSuspendAll destructor for Instrumentation observation");
  if(attempted)return Fail(error,"Instrumentation observation installation failed; restart process");
  attempted=true;
  SuspendAll storage{};
  construct(&storage,"artdcevm Instrumentation receiver observation",false);
  auto token=hooks.Install(target,observer,&original_storage,error);
  if(token&&!original_storage)std::abort();
  installed=token&&original_storage;
  destroy(&storage);
  return installed||Fail(error,"Instrumentation observation hook unavailable; restart process");
}
void MethodEntry(jvmtiEnv*,JNIEnv*,jthread,jmethodID) {}
struct OwnedEnvironment {
  jvmtiEnv* env=nullptr;
  bool enabled=false;
  // Consume ownership before cleanup: DisposeEnvironment must not be retried,
  // even when it reports an error. Always attempt disposal after disable failure.
  bool Close(std::string* error) {
    if(!env)return true;
    jvmtiEnv* owned=env;env=nullptr;
    const bool was_enabled=enabled;enabled=false;
    const auto disabled=was_enabled?
        owned->SetEventNotificationMode(JVMTI_DISABLE,JVMTI_EVENT_METHOD_ENTRY,nullptr):
        JVMTI_ERROR_NONE;
    const auto disposed=owned->DisposeEnvironment();
    if(disabled!=JVMTI_ERROR_NONE)
      return Fail(error,"Independent method-entry observation disable failed");
    if(disposed!=JVMTI_ERROR_NONE)
      return Fail(error,"Independent JVMTI environment disposal failed");
    return true;
  }
  // Earlier errors retain their cause and any pending JNI exception. This
  // fallback performs only JVMTI cleanup; publication requires explicit Close.
  ~OwnedEnvironment(){Close(nullptr);}
};

}
bool CalibrateRuntimeServices(HookEngine& hooks,JNIEnv* env,Snapshot* output,
                              std::string* error) {
  if(error)error->clear();
  if(!env||!output||active||env->ExceptionCheck())return Fail(error,"Invalid Runtime service calibration caller");
  if(!hooks.Initialize(error)||!runnable_executor::Initialize(hooks,env,error)||!Install(hooks,error))return false;
  runtime_receiver::Receivers observed;
  if(!runtime_receiver::Calibrate(hooks,env,&observed,error))return false;
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
  void* runtime=nullptr;
  if(!runtime_instance::Calibrate(hooks,observed.java_vm,&runtime,error))return false;
  if(env->ExceptionCheck())return Fail(error,"Pending JNI exception after Runtime service observation");
  if(!owned.Close(error))return false;
  // Every published receiver comes from its independent original public path.
  // Runtime identity is proven through public VM attachment, without reading
  // vendor/Mainline Runtime layout. No unobserved Heap/JIT slot is authorized.
  View view{};
  view.class_linker=observed.class_linker;view.jni_id_manager=observed.jni_id_manager;
  view.thread_list=observed.thread_list;view.java_vm=observed.java_vm;
  *output={runtime,observation.instrumentation,view};
  return true;
}
}
