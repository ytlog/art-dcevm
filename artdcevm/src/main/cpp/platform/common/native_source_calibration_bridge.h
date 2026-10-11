#pragma once
#include "platform/common/native_owned_removal.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/native_source_class_loading.h"
#include <optional>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace artdcevm::native_source_calibration {
// Additional passive observers share the sole actual Run; they grant no
// readiness by registration. Callbacks must preserve source arguments/results.
struct RunLifecycle {
 void* owner=nullptr;
 void (*begin)(void* driver,const runnable_jni::Context&,void*) noexcept=nullptr;
 void (*end)(void* driver,bool original_success,void*) noexcept=nullptr;
};
// Stack owner for an ordinary source call. The sole Run hook supplies its saved
// original body; no symbol address, SDK or standalone callback grants a proof.
class Bridge final {
 public:
  Bridge(const native_source_stop::Service& service,void* self,size_t extent) noexcept:
      service_(service),self_(self),extent_(extent) {
    native_owned_removal::PassiveObserver observer{this,Begin,Collected,End};
    registered_=self&&extent&&native_owned_removal::SetPassiveObserver(&observer);
  }
  Bridge(const native_source_stop::Service& service,void* self,size_t extent,
         native_source_class_loading::Service& loading,const runnable_jni::Context& context,
         const RunLifecycle& lifecycle={}) noexcept:
      Bridge(service,self,extent){loading_service_=&loading;context_=&context;
        if(lifecycle.owner&&lifecycle.begin&&lifecycle.end)lifecycle_=lifecycle;}
  ~Bridge(){
    if(lifecycle_started_){
      lifecycle_.end(lifecycle_driver_,false,lifecycle_.owner);lifecycle_started_=false;
      if(loading_)loading_->Complete(false);
      loading_.reset();observation_.reset();
    }
    if(registered_&&!native_owned_removal::SetPassiveObserver(nullptr))std::abort();
  }
  Bridge(const Bridge&)=delete;Bridge& operator=(const Bridge&)=delete;
  bool Authorized()const noexcept{return authorized_;}
  const native_source_stop::Service& Binding()const noexcept{return service_;}
  const char* Failure()const noexcept{return failure_;}
 private:
  static void Begin(void* driver,const void* body,void* self,void* raw) noexcept {
    auto& b=*static_cast<Bridge*>(raw);
    if(b.started_||self!=b.self_||!body){b.Refuse("ordinary-source-bridge-owner-mismatch");return;}
    b.started_=true;b.body_=body;
    // The actual ordinary Run entry is independently Runnable. Calibrate the
    // optional original C++ state scope before attaching passive observation;
    // the temporary real GC reservation is fully finished before Run proceeds.
    if(b.service_.RequiresOriginalWaiting()&&!b.service_.OriginalWaitingCalibrated()){
      std::string error;
      if(!b.service_.CalibrateOriginalWaiting(self,b.extent_,&error)){
        char detail[256];std::snprintf(detail,sizeof(detail),"ordinary-source-original-WaitingGC-scope-not-calibrated: %s",error.c_str());
        b.Refuse(detail);return;
      }
    }
    b.observation_.emplace(b.service_,driver,body,self,b.extent_);
    if(!b.observation_->Ready()){b.Refuse(b.observation_->Failure());return;}
    if(b.loading_service_){
      if(!b.context_){b.Refuse("ordinary-source-bridge-original-context-missing");return;}
      b.loading_.emplace(*b.loading_service_,driver,*b.context_);
      if(!b.observation_->BindCleanup(*b.loading_)){b.Refuse(b.observation_->Failure());return;}
      if(b.lifecycle_.begin){
        // Ready independently proved this exact source entry's current Thread
        // and real Runnable state; the pause owner matched driver/runtime/self.
        b.lifecycle_started_=true;b.lifecycle_driver_=driver;
        b.lifecycle_.begin(driver,*b.context_,b.lifecycle_.owner);
      }
    }
  }
  static void Collected(void* driver,const void* holder,void* raw) noexcept {
    auto& b=*static_cast<Bridge*>(raw);
    if(!b.observation_){b.Refuse("ordinary-source-bridge-missing-Run");return;}
    if(!b.observation_->Collected(driver,holder))b.Refuse(b.observation_->Failure());
  }
  static void End(void* driver,jvmtiError result,bool complete,void* raw) noexcept {
    auto& b=*static_cast<Bridge*>(raw);
    b.passive_available_=native_owned_removal::ReadPassiveTrace(&b.passive_);
    if(b.lifecycle_started_){
      if(driver!=b.lifecycle_driver_)std::abort();
      b.lifecycle_.end(driver,result==JVMTI_ERROR_NONE&&complete,b.lifecycle_.owner);
      b.lifecycle_started_=false;b.lifecycle_driver_=nullptr;
    }
    if(!b.observation_){b.Refuse("ordinary-source-bridge-missing-Run");return;}
    bool loading_success=true;
    if(b.loading_)loading_success=b.loading_->Complete(result==JVMTI_ERROR_NONE&&complete);
    native_source_stop::Calibration proof;
    b.authorized_=b.observation_->Finish(driver,result==JVMTI_ERROR_NONE&&complete&&loading_success,&proof)&&
        b.service_.Authorize(proof,b.body_);
    b.stop_=b.observation_->Trace();
    if(b.authorized_)b.failure_[0]=0;
    else b.Refuse(b.observation_->Failure()[0]?b.observation_->Failure():"ordinary-source-bridge-incomplete-transition-binding");
    // Releases the TLS owner even when source failed; Holder is never retained.
    b.observation_.reset();b.loading_.reset();
  }
  native_source_stop::Service service_;void* self_;size_t extent_;
  const void* body_=nullptr;
  std::optional<native_source_stop::Observation> observation_;
  native_source_class_loading::Service* loading_service_=nullptr;const runnable_jni::Context*context_=nullptr;
  std::optional<native_source_class_loading::Run> loading_;
  RunLifecycle lifecycle_;void* lifecycle_driver_=nullptr;bool lifecycle_started_=false;
  bool registered_=false,started_=false,authorized_=false;
  char failure_[768]="ordinary-source-bridge-not-observed",first_[128]{};
  bool passive_available_=false;
  native_owned_removal::PassiveTrace passive_{};
  native_source_stop::ObservationTrace stop_{};
  void Refuse(const char* reason) noexcept {
    if(!first_[0])std::snprintf(first_,sizeof(first_),"%s",reason);
    std::snprintf(failure_,sizeof(failure_),
      "%s [passive:available=%d mask=%x order=%llx events=%u truncated=%d raw=%u whole=%u first=%u returned=%d complete=%d result=%d self=%d/%d/%d; stop:mask=%x order=%llx events=%u truncated=%d phase=%u firstPhase=%u expectedRunnable=%u states=%u/%u/%u nativeStates=%u/%u/%u/%u/%u unreadable=%x currentMismatch=%x collected=%d/%d/%d counts=%u/%u/%u/%u/%u/%u/%u/%u stopPhases=%x resumePhases=%x]",
      first_,int(passive_available_),passive_.events.mask,static_cast<unsigned long long>(passive_.events.order),passive_.events.events,int(passive_.events.truncated),passive_.original_calls,passive_.whole_collections,unsigned(passive_.first_failure),int(passive_.returned),int(passive_.complete),int(passive_.result),int(passive_.begin_self_present),int(passive_.collected_same_thread),int(passive_.end_same_thread),
      stop_.events.mask,static_cast<unsigned long long>(stop_.events.order),stop_.events.events,int(stop_.events.truncated),unsigned(stop_.phase),unsigned(stop_.first_failure_phase),stop_.expected_runnable,stop_.states[0],stop_.states[1],stop_.states[7],stop_.states[2],stop_.states[3],stop_.states[4],stop_.states[5],stop_.states[6],stop_.state_unreadable,stop_.current_mismatch,int(stop_.collected_ready),int(stop_.collected_driver_match),int(stop_.collected_holder_present),stop_.counts[0],stop_.counts[1],stop_.counts[2],stop_.counts[3],stop_.counts[4],stop_.counts[5],stop_.counts[6],stop_.counts[7],stop_.phase_masks[3],stop_.phase_masks[4]);
  }
};
}
