#include "platform/android13/owned_jit_receiver_probe.h"
#include "platform/common/heap_collect_observer.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include <jvmti.h>
#include <mutex>
#include <chrono>
#include <thread>
#include "platform/android13/runtime_access_abi.h"
namespace artdcevm::android13 {
namespace {
namespace a=owned_jit_probe_abi;
struct Request {OwnedJitReceivers receivers;bool conflict=false;};
thread_local Request* active=nullptr;
a::Compile original_compile=nullptr;
std::mutex compilation_observation;
void* probe_method=nullptr;
void* probe_jit=nullptr;
bool probe_conflict=false;
std::mutex calibration_lock;
a::Sweep original_sweep=nullptr;
using JitScope=void (*)(a::SuspendJit*);
JitScope suspend_jit=nullptr,resume_jit=nullptr;
std::mutex installation;
bool attempted=false,installed=false;
void Observe(void*& slot,void* receiver) {
  if(!active)return;
  if(!receiver || (slot && slot!=receiver))active->conflict=true;
  else slot=receiver;
}
bool CompileProxy(void* jit,void* method,void* self,
                  jit_service::CompilationKind12 kind,bool prejit) {
  {
    std::lock_guard<std::mutex> lock(compilation_observation);
    // The rooted probe method, rather than the worker's TLS, identifies owned
    // compilation. JIT workers may compile asynchronously on another thread.
    if(probe_method && method==probe_method) {
      if(!jit || (probe_jit && probe_jit!=jit))probe_conflict=true;
      else probe_jit=jit;
    }
  }
  return original_compile(jit,method,self,kind,prejit);
}
void ObserveCollect(void* heap,bool,heap_collect_observer::Cause) {
  if(active)Observe(active->receivers.heap,heap);
}
void SweepProxy(void* cache,void* visitor) {
  if(active)Observe(active->receivers.code_cache,cache);
  original_sweep(cache,visitor);
}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
bool Install(HookEngine& hooks,std::string* error) {
  std::lock_guard<std::mutex> lock(installation);
  if(installed)return true;
  if(attempted)return Fail(error,"JIT receiver hooks previously failed; restart process");
  attempted=true;
  auto compile=hooks.FindSymbol("libart.so",a::kCompile);
  auto sweep=hooks.FindSymbol("libart.so",a::kSweep);
  auto jit_ctor=reinterpret_cast<JitScope>(hooks.FindSymbol("libart.so",a::kSuspend));
  auto jit_dtor=reinterpret_cast<JitScope>(hooks.FindSymbol("libart.so",a::kResume));
  using namespace abi::arm64::android_common;
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!compile || !sweep || !jit_ctor || !jit_dtor || !construct || !destroy)
    return Fail(error,"Missing named JIT/GC receiver observation functions");
  if(!heap_collect_observer::Install(hooks,ObserveCollect,error))return false;
  // Install unique observer proxies while original ART controls its mutators.
  // No compiler or GC is called with a guessed object as receiver.
  SuspendAll storage{};construct(&storage,"artdcevm JIT receiver observation",false);
  void* compile_original=nullptr;
  void* sweep_original=nullptr;
  auto compile_token=hooks.Install(compile,reinterpret_cast<void*>(CompileProxy),&compile_original,error);
  if(compile_token)original_compile=reinterpret_cast<a::Compile>(compile_original);
  auto sweep_token=compile_token && original_compile?
      hooks.Install(sweep,reinterpret_cast<void*>(SweepProxy),&sweep_original,error):nullptr;
  if(sweep_token)original_sweep=reinterpret_cast<a::Sweep>(sweep_original);
  installed=compile_token && sweep_token && original_compile && original_sweep;
  if(installed){suspend_jit=jit_ctor;resume_jit=jit_dtor;}
  destroy(&storage);
  if(!installed)return Fail(error,"JIT/GC observation hooks unavailable; restart process");
  return true;
}
}
bool CalibrateOwnedJitReceivers(HookEngine& hooks,JNIEnv* env,
                               jobject calibration,void* observed_jni_manager,
                               OwnedJitReceivers* output,std::string* error) {
  if(error)error->clear();
  if(!env || !output || !calibration || !observed_jni_manager || active)return Fail(error,"Invalid or reentrant JIT receiver calibration");
  if(env->ExceptionCheck())return Fail(error,"JIT receiver calibration requires no pending exception");
  std::lock_guard<std::mutex> lock(calibration_lock);
  if(!Install(hooks,error))return false;
  jclass owner=env->GetObjectClass(calibration);
  if(!owner)return Fail(error,"Missing owned JIT calibration class");
  jmethodID id=env->GetMethodID(owner,"compute","()I");
  using Decode=void* (*)(void*,jmethodID);
  auto decode=reinterpret_cast<Decode>(hooks.FindSymbol("libart.so",runtime_access_abi::kDecodeMethodId));
  void* method=id && decode?decode(observed_jni_manager,id):nullptr;
  if(!method || env->ExceptionCheck()) {
    env->DeleteLocalRef(owner);
    return Fail(error,"Missing owned JIT probe method identity");
  }
  {
    std::lock_guard<std::mutex> observe(compilation_observation);
    probe_method=method;probe_jit=nullptr;probe_conflict=false;
  }
  bool java_ok=true;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  do {
    for(int i=0;i<2048;++i) {
      // EvolutionCalibration.compute is side effect free and returns 38.
      const int result=env->CallIntMethod(calibration,id);
      if(env->ExceptionCheck() || result!=38) {java_ok=false;break;}
    }
    bool seen;
    {std::lock_guard<std::mutex> observe(compilation_observation);seen=probe_jit!=nullptr;}
    if(!java_ok || seen)break;
    std::this_thread::yield();
  }while(std::chrono::steady_clock::now()<deadline);
  Request request;
  {
    std::lock_guard<std::mutex> observe(compilation_observation);
    request.receivers.jit=probe_jit;request.conflict=probe_conflict;
    probe_method=nullptr;
  }
  env->DeleteLocalRef(owner);
  if(!java_ok)return Fail(error,"Owned Java JIT probe failed");
  JavaVM* vm=nullptr;jvmtiEnv* temporary=nullptr;
  if(env->GetJavaVM(&vm)!=JNI_OK || !vm ||
     vm->GetEnv(reinterpret_cast<void**>(&temporary),JVMTI_VERSION_1_2)!=JNI_OK || !temporary)
    return Fail(error,"Independent JVMTI environment unavailable for JIT receiver calibration");
  active=&request;
  a::SuspendJit storage{};
  suspend_jit(&storage);
  resume_jit(&storage);
  // Public ForceGC obtains Heap internally. The original collector invokes
  // Runtime::SweepSystemWeaks -> original JitCodeCache::SweepRootTables. TLS
  // accepts the owned synchronous call only, never unrelated background work.
  const auto collection=temporary->ForceGarbageCollection();
  active=nullptr;
  const auto disposed=temporary->DisposeEnvironment();
  if(collection!=JVMTI_ERROR_NONE || disposed!=JVMTI_ERROR_NONE || request.conflict ||
     !request.receivers.jit || !request.receivers.code_cache || !request.receivers.heap) {
    if(error)*error="Owned ART receiver calibration failed: jit="+
        std::to_string(request.receivers.jit!=nullptr)+", cache="+
        std::to_string(request.receivers.code_cache!=nullptr)+", heap="+
        std::to_string(request.receivers.heap!=nullptr)+", suspended="+
        std::to_string(storage.was_on)+", conflict="+std::to_string(request.conflict)+
        ", collection="+std::to_string(collection)+", disposal="+std::to_string(disposed);
    return false;
  }
  *output=request.receivers;
  return true;
}
}
