#include "platform/android13/runtime_receiver_probe.h"
#include "platform/common/runtime_receiver_probe.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include <jvmti.h>
#include <mutex>
namespace artdcevm::android13 {
namespace {
using AddListener=void (*)(void*,void*,uint32_t);
constexpr char kAddListener[]="_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEj";
struct Request {RuntimeReceivers receivers;bool conflicting=false;};
thread_local Request* active=nullptr;
AddListener original_add_listener=nullptr;
std::mutex installation;
bool attempted=false,installed=false;
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
void ObserveListener(void* instrumentation,void* listener,uint32_t events) {
  if(active) {
    auto& observed=active->receivers.instrumentation;
    if(!instrumentation || (observed && observed!=instrumentation))active->conflicting=true;
    else observed=instrumentation;
  }
  original_add_listener(instrumentation,listener,events);
}
bool Install(HookEngine& hooks,std::string* error) {
  std::lock_guard<std::mutex> lock(installation);
  if(installed)return true;
  if(attempted)return Fail(error,"Instrumentation receiver installation failed; restart process");
  auto target=hooks.FindSymbol("libart.so",kAddListener);
  using namespace abi::arm64::android_common;
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!target || !construct || !destroy)return Fail(error,"Missing named legacy Instrumentation observation functions");
  attempted=true;
  SuspendAll storage{};construct(&storage,"artdcevm legacy Instrumentation receiver",false);
  void* trampoline=nullptr;
  auto token=hooks.Install(target,reinterpret_cast<void*>(ObserveListener),&trampoline,error);
  original_add_listener=reinterpret_cast<AddListener>(trampoline);
  installed=token && original_add_listener;
  destroy(&storage);
  return installed || Fail(error,"Legacy Instrumentation observation hook unavailable");
}
}
bool SetRuntimeFindClassHandler(void* handler,void** original) {
  return runtime_receiver::SetFindClassHandler(handler,original);
}
bool CalibrateRuntimeReceivers(HookEngine& hooks,JNIEnv* env,RuntimeReceivers* output,
                              std::string* error) {
  if(error)error->clear();
  if(!env || !output || active)return Fail(error,"Invalid or reentrant Runtime receiver calibration");
  runtime_receiver::Receivers observed;
  if(!runtime_receiver::Calibrate(hooks,env,&observed,error) || !Install(hooks,error))return false;
  Request request;
  request.receivers={observed.class_linker,observed.jni_id_manager,observed.java_vm,observed.thread_list,nullptr};
  // Fixed OpenjdkJvmTi GetEnvHandler creates a distinct environment on every
  // request. Never modify the bootstrap/client environments or their callbacks.
  // The temporary environment owns its capability and event notification state.
  jvmtiEnv* temporary=nullptr;
  if(request.receivers.java_vm->GetEnv(reinterpret_cast<void**>(&temporary),JVMTI_VERSION_1_2)!=JNI_OK || !temporary)
    return Fail(error,"Independent JVMTI environment unavailable for Instrumentation observation");
  jvmtiCapabilities capabilities{};capabilities.can_generate_method_entry_events=1;
  auto capability_result=temporary->AddCapabilities(&capabilities);
  jvmtiError enable_result=JVMTI_ERROR_NOT_AVAILABLE;
  jvmtiError disable_result=JVMTI_ERROR_NONE;
  if(capability_result==JVMTI_ERROR_NONE) {
    active=&request;
    enable_result=temporary->SetEventNotificationMode(JVMTI_ENABLE,JVMTI_EVENT_METHOD_ENTRY,nullptr);
    active=nullptr;
    if(enable_result==JVMTI_ERROR_NONE)
      disable_result=temporary->SetEventNotificationMode(JVMTI_DISABLE,JVMTI_EVENT_METHOD_ENTRY,nullptr);
  }
  auto dispose_result=temporary->DisposeEnvironment();
  if(capability_result!=JVMTI_ERROR_NONE || enable_result!=JVMTI_ERROR_NONE ||
     disable_result!=JVMTI_ERROR_NONE || dispose_result!=JVMTI_ERROR_NONE ||
     request.conflicting || !request.receivers.instrumentation)
    return Fail(error,"Owned JVMTI notification did not establish an Instrumentation receiver or cleanup failed");
  *output=request.receivers;
  return true;
}
}
