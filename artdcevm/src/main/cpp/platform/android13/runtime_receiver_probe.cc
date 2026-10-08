#include "platform/android13/runtime_receiver_probe.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cstring>
#include <atomic>
#include <mutex>
#include <jvmti.h>
namespace artdcevm::android13 {
namespace {
// Fixed class_linker.h/handle.h: Handle<ClassLoader> is one StackReference slot
// pointer passed by value; ObjPtr<Class> is the trivial single-word return.
using FindClass=abi::arm64::android11::ReleaseObjectPointer (*)(void*,void*,const char*,void*);
using EncodeMethod=jmethodID (*)(void*,void*);
using SuspendThreads=void (*)(void*,const char*,bool);
using AddListener=void (*)(void*,void*,uint32_t);
constexpr char kAddListener[]="_ZN3art15instrumentation15Instrumentation11AddListenerEPNS0_23InstrumentationListenerEj";
constexpr char kSuspendThreads[]="_ZN3art10ThreadList10SuspendAllEPKcb";
constexpr char kFindClass[]="_ZN3art11ClassLinker9FindClassEPNS_6ThreadEPKcNS_6HandleINS_6mirror11ClassLoaderEEE";
constexpr char kEncodeMethod[]="_ZN3art3jni12JniIdManager14EncodeMethodIdEPNS_9ArtMethodE";
constexpr char kClass[]="com/github/ytlog/artdcevm/internal/CalibrationFixture";
constexpr char kDescriptor[]="Lcom/github/ytlog/artdcevm/internal/CalibrationFixture;";
struct Request {RuntimeReceivers receivers;bool conflicting=false;};
thread_local Request* active=nullptr;
FindClass original_find=nullptr;
EncodeMethod original_encode=nullptr;
SuspendThreads original_suspend=nullptr;
AddListener original_add_listener=nullptr;
abi::arm64::android_common::SuspendConstruct suspend_construct=nullptr;
abi::arm64::android_common::SuspendDestroy suspend_destroy=nullptr;
std::atomic<FindClass> find_handler{nullptr};
std::mutex installation;
bool attempted=false,installed=false;
void Observe(void*& slot,void* receiver,Request& request) {
  if(!receiver || (slot && slot!=receiver))request.conflicting=true;
  else slot=receiver;
}
abi::arm64::android11::ReleaseObjectPointer ObserveFind(void* linker,void* self,
                                                     const char* descriptor,void* loader) {
  if(active && descriptor && !std::strcmp(descriptor,kDescriptor))
    Observe(active->receivers.class_linker,linker,*active);
  auto handler=find_handler.load(std::memory_order_acquire);
  return (handler?handler:original_find)(linker,self,descriptor,loader);
}
jmethodID ObserveEncode(void* manager,void* method) {
  if(active)Observe(active->receivers.jni_id_manager,manager,*active);
  return original_encode(manager,method);
}
void ObserveSuspend(void* threads,const char* cause,bool long_suspend) {
  if(active)Observe(active->receivers.thread_list,threads,*active);
  original_suspend(threads,cause,long_suspend);
}
void ObserveListener(void* instrumentation,void* listener,uint32_t events) {
  if(active)Observe(active->receivers.instrumentation,instrumentation,*active);
  original_add_listener(instrumentation,listener,events);
}
bool Fail(std::string* error,const char* message) {if(error)*error=message;return false;}
bool Install(HookEngine& hooks,std::string* error) {
  std::lock_guard<std::mutex> lock(installation);
  if(installed)return true;
  if(attempted)return Fail(error,"Runtime receiver hook installation previously failed; restart process");
  attempted=true;
  auto find=hooks.FindSymbol("libart.so",kFindClass);
  auto encode=hooks.FindSymbol("libart.so",kEncodeMethod);
  auto suspend=hooks.FindSymbol("libart.so",kSuspendThreads);
  auto add_listener=hooks.FindSymbol("libart.so",kAddListener);
  using namespace abi::arm64::android_common;
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!find || !encode || !suspend || !add_listener || !construct || !destroy)
    return Fail(error,"Missing named Runtime receiver observation functions");
  // Entry is a regular Native JNI call. ScopedSuspendAll obtains the original
  // ThreadList itself, so no uncalibrated Thread or Runtime receiver is passed.
  SuspendAll storage{};construct(&storage,"artdcevm Runtime receiver calibration",false);
  void* find_original=nullptr;void* encode_original=nullptr;
  auto find_token=hooks.Install(find,reinterpret_cast<void*>(ObserveFind),&find_original,error);
  if(find_token)original_find=reinterpret_cast<FindClass>(find_original);
  auto encode_token=find_token && original_find?
      hooks.Install(encode,reinterpret_cast<void*>(ObserveEncode),&encode_original,error):nullptr;
  if(encode_token)original_encode=reinterpret_cast<EncodeMethod>(encode_original);
  void* suspend_original=nullptr;
  auto suspend_token=encode_token && original_encode?
      hooks.Install(suspend,reinterpret_cast<void*>(ObserveSuspend),&suspend_original,error):nullptr;
  if(suspend_token)original_suspend=reinterpret_cast<SuspendThreads>(suspend_original);
  void* listener_original=nullptr;
  auto listener_token=suspend_token && original_suspend?
      hooks.Install(add_listener,reinterpret_cast<void*>(ObserveListener),&listener_original,error):nullptr;
  if(listener_token)original_add_listener=reinterpret_cast<AddListener>(listener_original);
  installed=find_token && encode_token && suspend_token && listener_token &&
      original_find && original_encode && original_suspend && original_add_listener;
  if(installed){suspend_construct=construct;suspend_destroy=destroy;}
  destroy(&storage);
  if(!installed)return Fail(error,"Runtime receiver observation hooks unavailable; restart process");
  return true;
}
}
bool SetRuntimeFindClassHandler(void* handler,void** original) {
  std::lock_guard<std::mutex> lock(installation);
  if(!installed || (handler && !original))return false;
  if(original)*original=reinterpret_cast<void*>(original_find);
  find_handler.store(reinterpret_cast<FindClass>(handler),std::memory_order_release);
  return true;
}
bool CalibrateRuntimeReceivers(HookEngine& hooks,JNIEnv* env,RuntimeReceivers* output,
                              std::string* error) {
  if(error)error->clear();
  if(!env || !output || active)return Fail(error,"Invalid or reentrant Runtime receiver calibration");
  if(env->ExceptionCheck())return Fail(error,"Runtime receiver calibration requires no pending exception");
  if(!Install(hooks,error))return false;
  Request request;
  active=&request;
  auto klass=env->FindClass(kClass);
  auto method=klass?env->GetMethodID(klass,"compute","()I"):nullptr;
  // C2 obtains the real ThreadList internally. Observe its named SuspendAll
  // receiver, and immediately release the owned stop before any public JNI work.
  abi::arm64::android_common::SuspendAll storage{};
  suspend_construct(&storage,"artdcevm ThreadList receiver calibration",false);
  suspend_destroy(&storage);
  active=nullptr;
  if(klass)env->DeleteLocalRef(klass);
  if(!klass || !method || env->ExceptionCheck() || request.conflicting ||
     !request.receivers.class_linker || !request.receivers.jni_id_manager || !request.receivers.thread_list)
    return Fail(error,"Public JNI did not establish consistent ClassLinker/JNI-ID receivers");
  if(env->GetJavaVM(&request.receivers.java_vm)!=JNI_OK || !request.receivers.java_vm)
    return Fail(error,"Public JNI JavaVM receiver unavailable");
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
