#pragma once
#include "platform/common/native_owned_removal.h"
#include "platform/common/native_source_stop.h"
#include <optional>
#include <cstdlib>

namespace artdcevm::native_source_calibration {
// Stack owner for an ordinary source call. The sole Run hook supplies its saved
// original body; no symbol address, SDK or standalone callback grants a proof.
class Bridge final {
 public:
  Bridge(const native_source_stop::Service& service,void* self,size_t extent) noexcept:
      service_(service),self_(self),extent_(extent) {
    native_owned_removal::PassiveObserver observer{this,Begin,Collected,End};
    registered_=self&&extent&&native_owned_removal::SetPassiveObserver(&observer);
  }
  ~Bridge(){
    if(registered_&&!native_owned_removal::SetPassiveObserver(nullptr))std::abort();
  }
  Bridge(const Bridge&)=delete;Bridge& operator=(const Bridge&)=delete;
  bool Authorized()const noexcept{return authorized_;}
  const native_source_stop::Service& Binding()const noexcept{return service_;}
  const char* Failure()const noexcept{return failure_;}
 private:
  static void Begin(void* driver,const void* body,void* self,void* raw) noexcept {
    auto& b=*static_cast<Bridge*>(raw);
    if(b.started_||self!=b.self_||!body){b.failure_="ordinary-source-bridge-owner-mismatch";return;}
    b.started_=true;b.body_=body;
    b.observation_.emplace(b.service_,driver,body,self,b.extent_);
    if(!b.observation_->Ready())b.failure_=b.observation_->Failure();
  }
  static void Collected(void* driver,const void* holder,void* raw) noexcept {
    auto& b=*static_cast<Bridge*>(raw);
    if(!b.observation_){b.failure_="ordinary-source-bridge-missing-Run";return;}
    if(!b.observation_->Collected(driver,holder))b.failure_=b.observation_->Failure();
  }
  static void End(void* driver,jvmtiError result,bool complete,void* raw) noexcept {
    auto& b=*static_cast<Bridge*>(raw);
    if(!b.observation_){b.failure_="ordinary-source-bridge-missing-Run";return;}
    native_source_stop::Calibration proof;
    b.authorized_=b.observation_->Finish(driver,result==JVMTI_ERROR_NONE&&complete,&proof)&&
        b.service_.Authorize(proof,b.body_);
    b.failure_=b.authorized_?"":b.observation_->Failure();
    if(!b.authorized_&&!b.failure_[0])b.failure_="ordinary-source-bridge-incomplete-transition-binding";
    // Releases the TLS owner even when source failed; Holder is never retained.
    b.observation_.reset();
  }
  native_source_stop::Service service_;void* self_;size_t extent_;
  const void* body_=nullptr;
  std::optional<native_source_stop::Observation> observation_;
  bool registered_=false,started_=false,authorized_=false;
  const char* failure_="ordinary-source-bridge-not-observed";
};
}
