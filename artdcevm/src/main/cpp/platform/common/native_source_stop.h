#pragma once
#include "api/hook_engine.h"
#include "platform/common/thread_suspend_service.h"
#include "platform/abi/arm64/android-common/source_stop.h"
namespace artdcevm::native_source_stop {
class Session;
// Install one shared observer/transfer for the original empty native
// ScopedSuspendAll carrier. Never install a second hook at these same leaves.
bool Install(HookEngine&,std::string* error);
class Service {
 public:
  // heap is the independent actual runtime receiver; thread_state is calibrated
  // from original native/public observations, never selected by an API number.
  static bool Bind(HookEngine&,void* heap,const thread_suspend_service::Service& thread_state,
                   Service*,std::string* error);
 private:
  void* heap_=nullptr;void** mutator_=nullptr;void* (*current_)()=nullptr;
  abi::arm64::android_common::source_stop::StartGC start_=nullptr;
  abi::arm64::android_common::source_stop::FinishGC finish_=nullptr;
  abi::arm64::android_common::source_stop::MutatorOperation lock_=nullptr,unlock_=nullptr;
  abi::arm64::android_common::SuspendConstruct stop_=nullptr;
  abi::arm64::android_common::SuspendDestroy resume_=nullptr;
  thread_suspend_service::Service state_;
  friend class Session;
};
// Construct/stage this owner BEFORE the refundable Collection callback. Begin
// only after all managed allocation/root registration has finished. Every root
// and native service used afterward must already be prepared and owned.
// On accepted scan ContinueSource preserves actual global suspend counts and GC
// exclusion while only releasing exclusive mutator access for source self.
// No other Java thread may run or attach runnable. The original final native
// scope takes over the SAME reservation; nesting another SuspendAll deadlocks.
class Session {
 public:
  explicit Session(const Service& service):service_(service){}
  ~Session();
  Session(const Session&)=delete;Session& operator=(const Session&)=delete;
  bool Begin(void* self,size_t mapped_extent) noexcept;
  bool ContinueSource() noexcept;
  bool IsStopped(void* self)const noexcept;
  bool Completed()const noexcept{return phase_==Phase::kCompleted;}
  bool Reserved()const noexcept{return gc_&&suspended_;}
  const char* Failure()const noexcept{return failure_;}
  // For the sole source adapter; after ContinueSource any failure is an
  // invariant violation, not a new refundable refusal. Never call JNI here.
  static bool TransferConstruct(void*,const char*,bool) noexcept;
  static bool TransferDestroy(void*) noexcept;
 private:
  enum class Phase {kEmpty,kStopped,kContinuing,kFinal,kCompleted,kRolledBack};
  bool SameOwner()const noexcept;
  bool Native()const noexcept;
  void Rollback() noexcept;
  Service service_;void* self_=nullptr;size_t extent_=0;
  abi::arm64::android_common::SuspendAll storage_{};
  void* source_carrier_=nullptr;
  Phase phase_=Phase::kEmpty;bool gc_=false,suspended_=false,exclusive_=false;
  const char* failure_="not-started";
};
inline bool IsStopped(const void* state,void* self) noexcept {
 return state&&static_cast<const Session*>(state)->IsStopped(self);
}
}
