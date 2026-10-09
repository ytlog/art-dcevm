#pragma once
#include "platform/common/bounded_native_event_trace.h"
#include "api/hook_engine.h"
#include "platform/common/thread_suspend_service.h"
#include "platform/abi/arm64/android-common/source_stop.h"
namespace artdcevm::native_source_class_loading {class Run;}
namespace artdcevm::native_source_stop {
class Session;
class Observation;
class Calibration;
struct NativeHooks;
// Install one shared observer/transfer for the original empty native
// ScopedSuspendAll carrier. Never install a second hook at these same leaves.
bool Install(HookEngine&,std::string* error);
class Service {
 public:
  // Binding supports passive legacy observation without a pure pair. Authorize
  // and Begin still require that complete independently calibrated pair.
  // heap is the independent actual runtime receiver; thread_state is calibrated
  // from original native/public observations, never selected by an API number.
  static bool Bind(HookEngine&,void* heap,const thread_suspend_service::Service& thread_state,
                   Service*,std::string* error);
  bool Authorize(const Calibration&,const void* exact_original_run) noexcept;
  bool Calibrated()const noexcept{return calibrated_;}
  // Borrow only the actual original final carrier; requires no direct transition pair.
  bool AuthorizeOriginalFinal(const Calibration&,const void* exact_original_run) noexcept;
  bool OriginalFinalCalibrated()const noexcept{return original_calibrated_;}
 private:
  bool SameBinding(const Service&)const noexcept;
  bool calibrated_=false,original_calibrated_=false;const void* original_run_=nullptr;char final_cause_[128]{};bool final_long_=false;
  void* heap_=nullptr;void* mutator_identity_=nullptr;void** mutator_=nullptr;void* (*current_)()=nullptr;
  abi::arm64::android_common::source_stop::StartGC start_=nullptr;
  abi::arm64::android_common::source_stop::FinishGC finish_=nullptr;
  abi::arm64::android_common::source_stop::MutatorOperation lock_=nullptr,unlock_=nullptr;
  abi::arm64::android_common::SuspendConstruct stop_=nullptr;
  abi::arm64::android_common::SuspendDestroy resume_=nullptr;
  thread_suspend_service::Service state_;
  friend class Session;friend class Observation;friend class Calibration;
};
// Private sealed native witness. Fixed declarations/symbol presence cannot create
// this permission; only the actual paired ordinary source lifecycle below can.
class Calibration final {
 public:
  bool Valid()const noexcept{return valid_;}
 private:
  Service binding_;const void* run_body_=nullptr;
  char cause_[128]{};bool long_=false,valid_=false;
  friend class Observation;friend class Service;
};
// The sole original Run owner constructs this before entering actual Run and
// arms it only after exact original whole-HOLDER Collected success. This has no
// allocation/JNI behavior and never modifies an ordinary source call. Every
// original heap/suspend operation still runs; failures merely prevent sealing.
struct ObservationTrace {
 // begin=1,collected=2,disable=3,stop=4,resume=5,enable=6,GC=7,finish=8.
 BoundedNativeEventTrace events;
 uint32_t counts[8]{};uint32_t phase_masks[8]{};
 uint32_t states[8]{UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
 uint32_t state_unreadable=0,current_mismatch=0,expected_runnable=UINT32_MAX;
 uint8_t phase=0,first_failure_phase=0;
 bool failed=false,collected_ready=false,collected_driver_match=false,collected_holder_present=false;
};
class Observation final {
 public:
  Observation(const Service&,void* actual_driver,const void* exact_original_run,
              void* self,size_t mapped_extent,native_source_class_loading::Run* cleanup=nullptr) noexcept;
  ~Observation();
  Observation(const Observation&)=delete;Observation&operator=(const Observation&)=delete;
  bool Ready()const noexcept;
  bool BindCleanup(native_source_class_loading::Run&)noexcept;
  bool Collected(void* exact_driver,const void* actual_holder) noexcept;
  bool Finish(void* exact_driver,bool original_success,Calibration*) noexcept;
  const char* Failure()const noexcept{return failure_;}
  const ObservationTrace& Trace()const noexcept{return trace_;}
 private:
  friend struct NativeHooks;
  static void DisableBegin(void*,void*) noexcept;
  static void DisableEnd(void*,void*) noexcept;
  static void Constructed(void*,const char*,bool) noexcept;
  static void Destroyed(void*) noexcept;
  static void GCOperation(void* heap,void* self,bool starting) noexcept;
  bool State(uint32_t)const noexcept;
  enum class Phase{kRun,kArmed,kDisabled,kStopped,kResumed,kComplete,kCleanupStopped,kCleanupResumed};
  Service service_;void* driver_=nullptr;const void* run_body_=nullptr;
  void* self_=nullptr;size_t extent_=0;const void* holder_=nullptr;void* carrier_=nullptr;
  char cause_[128]{};bool long_=false,attached_=false,failed_=false,finished_=false;
  Phase phase_=Phase::kRun;const char* failure_="ordinary-source-observation-not-started";
  bool prefinal_gc_=false;
  native_source_class_loading::Run* cleanup_=nullptr;bool postfinal_gc_=false;
  ObservationTrace trace_{};
  void Event(uint8_t) noexcept;
  void Refuse(const char*) noexcept;
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
  // Refund only before handing the reservation to original source. Restore
  // actual Runnable while keeping this witness alive for dependent readers.
  bool RollbackBeforeSource() noexcept;
  // No reservation or mutation permission is created here. The sole actual Run
  // owner attaches before calling the exact calibrated original body.
  bool PrepareOriginalFinal(void* self,size_t mapped_extent,const void* exact_original_run,
                            void* exact_driver) noexcept;
  bool OriginalCollected(void* exact_driver,const void* actual_holder) noexcept;
  bool BorrowsOriginalFinal()const noexcept{return borrowed_;}
  bool IsStopped(void* self)const noexcept;
  bool IsCollectedStop(void* self)const noexcept;
  bool IsFinalWindow(void* self)const noexcept;
  bool Completed()const noexcept{return phase_==Phase::kCompleted;}
  bool Reserved()const noexcept{return gc_&&suspended_;}
  const char* Failure()const noexcept{return failure_;}
 private:
  friend struct NativeHooks;
  // Only the sole original native hook can transfer reservations; after
  // ContinueSource failure is an invariant violation, not a refundable refusal.
  static bool TransferConstruct(void*,const char*,bool) noexcept;
  static bool TransferDestroy(void*) noexcept;
  static void SourceDisableBegin(void*,void*) noexcept;
  static void SourceDisableEnd(void*,void*,bool completed) noexcept;
  static void UnexpectedGC() noexcept;
  static void OriginalDisabled(void*,void*) noexcept;
  static void OriginalConstructed(void*,const char*,bool) noexcept;
  static void OriginalDestroying(void*) noexcept;
  static void OriginalDestroyed(void*) noexcept;
  static void OriginalEnabled(void*,void*) noexcept;
  enum class Phase {kEmpty,kStopped,kContinuing,kFinal,kFinalResumed,kCompleted,kRolledBack,kOriginalPrepared,kOriginalArmed,kOriginalDisabled,kOriginalClosing};
  bool SameOwner()const noexcept;
  bool Native()const noexcept;
  void Rollback() noexcept;
  Service service_;void* self_=nullptr;size_t extent_=0;
  abi::arm64::android_common::SuspendAll storage_{};
  void* source_carrier_=nullptr;
  Phase phase_=Phase::kEmpty;bool gc_=false,suspended_=false,exclusive_=false,source_disable_=false;
  bool borrowed_=false,borrowed_failed_=false;
  void* original_driver_=nullptr;const void* original_holder_=nullptr;
  void RefuseOriginal(const char*) noexcept;
  const char* failure_="not-started";
};
inline bool IsStopped(const void* state,void* self) noexcept {
 return state&&static_cast<const Session*>(state)->IsStopped(self);
}
}
