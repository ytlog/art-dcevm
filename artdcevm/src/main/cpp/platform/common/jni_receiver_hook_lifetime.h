#pragma once
#include "platform/common/jni_receiver_calibration.h"
#include "platform/abi/common/suspend_all.h"
#include <atomic>
#include <cstdlib>
#include <mutex>
namespace artdcevm::jni_receiver_hook_lifetime {
// Pin must retain both this installer/dispatcher image and the HookEngine until
// process exit. Objects and installed hooks are deliberately never destructed.
using Pin=bool (*)(void*,std::string*);
template<size_t Width> class Installer {
 public:
  // Call ONLY from an ordinary non-critical JNI native entry, outside any
  // borrowed Runnable callback. ART supplies Native state at that entry. The
  // named SuspendAll scope blocks every later JNI Runnable transition while
  // the original trampoline and dispatcher are published; no Thread field is
  // read/written. Lifetime/extent proof precedes any hook installation.
  bool InitializeFromNative(HookEngine& hooks,JNIEnv* env,jobject carrier,size_t extent,
                            Pin pin,void* pin_data,std::string* error,
                            jni_receiver_calibration::Report* observation=nullptr,borrowed_jni_callback::Operation operation=nullptr,void* data=nullptr) {
    if(observation)*observation={jni_receiver_calibration::Result::kUnavailable,
        jni_receiver_calibration::CallbackResult::kUnavailable};
    // Reject a known borrowed callback before acquiring the install mutex: it
    // is Runnable, and recursively installing while a calibration owns the
    // mutex would otherwise deadlock instead of preserving JNI behavior.
    if(borrowed_jni_callback::Bridge::HasActiveInvocation())
      return Fail(error,"Cannot install from a borrowed Runnable JNI callback");
    std::lock_guard<std::mutex> guard(install_);
    if(!env || !carrier || !pin || env->ExceptionCheck())return Fail(error,"Native JNI entry or lifetime pin unavailable");
    if(!bound_.load(std::memory_order_acquire)) {
      if(!hooks.Initialize(error))return false;
      auto resolve=[&](const char* name){return hooks.FindSymbol("libart.so",name);};
      auto current=reinterpret_cast<abi::common::CurrentThread>(resolve(abi::common::kCurrentThread));
      auto suspend=reinterpret_cast<abi::common::SuspendConstruct>(resolve(abi::common::kSuspendConstruct));
      auto resume=reinterpret_cast<abi::common::SuspendDestroy>(resolve(abi::common::kSuspendDestroy));
      void* target=resolve(abi::common::kFromReflectedMethod);
      JavaVM* vm=nullptr;void* self=current?current():nullptr;
      jni_receiver_metadata::Receiver prefix{};
      if(!current || !suspend || !resume || !target || !self ||
          env->GetJavaVM(&vm)!=JNI_OK || env->ExceptionCheck() || !vm ||
          !jni_receiver_metadata::ReadMatchedPrefix(Width,env,extent,
              reinterpret_cast<uintptr_t>(self),reinterpret_cast<uintptr_t>(vm),&prefix))
        return Fail(error,"Missing named suspend/JNI receivers or independent Native owner proof");
      if(dispatch_.load(std::memory_order_acquire) || !pin(pin_data,error))
        return Fail(error,"Process-lifetime dispatcher pin unavailable");
      abi::common::SuspendAll storage{};
      suspend(&storage,"artdcevm ARM JNI receiver publication",false);
      struct Resume {abi::common::SuspendDestroy body;void* storage;~Resume(){body(storage);}} release{resume,&storage};
      dispatch_.store(this,std::memory_order_release);
      void* original=nullptr;
      void* token=hooks.Install(target,reinterpret_cast<void*>(Proxy),&original,error);
      jni_receiver_calibration::Service<Width> candidate;
      if(!token || !jni_receiver_calibration::Service<Width>::Bind(hooks,original,&candidate,error)) {
        // An unbound replacement cannot legally be resumed. Roll it back while
        // all Runnable threads remain suspended; failed rollback is fatal.
        if(token && !hooks.Remove(token,error))std::abort();
        dispatch_.store(nullptr,std::memory_order_release);
        return Fail(error,"Original trampoline publication failed before resume");
      }
      service_=candidate;token_=token;
      bound_.store(true,std::memory_order_release);
      // Scope destructor resumes only after original and dispatcher publication.
    }
    auto report=service_.Calibrate(env,carrier,extent,operation,data);
    if(observation)*observation=report;
    if(report.result!=jni_receiver_calibration::Result::kSuccess)
      return Fail(error,"Original Runnable callback/receiver calibration refused");
    ready_.store(true,std::memory_order_release);
    if(error)error->clear();return true;
  }
  bool Bound() const {return bound_.load(std::memory_order_acquire);}
  bool Ready() const {return ready_.load(std::memory_order_acquire);}
 private:
  static bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
  static void* Proxy(const abi::common::RunnableJniScope& scope,jobject carrier) {
    auto* owner=dispatch_.load(std::memory_order_acquire);
    // Reaching this before publication violates the proved exclusive handoff;
    // never return a fabricated ArtMethod pointer to ART.
    if(!owner || !owner->bound_.load(std::memory_order_acquire))std::abort();
    return owner->service_.OnFromReflectedMethod(scope,carrier);
  }
  inline static std::atomic<Installer*> dispatch_{nullptr};
  jni_receiver_calibration::Service<Width> service_;
  std::mutex install_;
  std::atomic<bool> bound_{false},ready_{false};
  void* token_=nullptr;
};
}
