#pragma once
#include "platform/common/native_initializer_state_calibration.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/runtime_services.h"
#include "platform/abi/arm64/android11/initializer_transaction.h"
namespace artdcevm::native_initializer_tid_writer {
// A source writer capability is deliberately separate from samePlan active
// initialization transfer, monitor/alias/frame recovery and removal authority.
class Service final {
 public:
  Service()=default;Service(const Service&)=delete;Service&operator=(const Service&)=delete;
  bool Calibrate(HookEngine&,JNIEnv*,jobject strong_executable,
      const modern_field_metadata::FieldMetadata&,const source_handle_roots::Service&,
      const runtime_services::Snapshot&,std::string*);
  bool ReadyFor(const modern_field_metadata::FieldMetadata&,const source_handle_roots::Service&)const noexcept;
  // Final use additionally requires a separate exact Plan active authority.
  // No public write method is exposed by this baseline calibration service.
  bool InactiveTransaction()const noexcept;
 private:
  bool RoundTrip(const runnable_jni::Context&,jobject,uint32_t)noexcept;
  friend struct Producer;
  const modern_field_metadata::FieldMetadata*metadata_=nullptr;
  const source_handle_roots::Service*roots_=nullptr;
  native_initializer_state::Calibration state_;
  void*runtime_=nullptr;
  abi::arm64::android11::initializer_transaction::ActiveTransaction active_=nullptr;
  abi::arm64::android11::initializer_transaction::RecordWrite32 record_=nullptr;
  abi::arm64::android11::initializer_transaction::SetClinitTid setter_=nullptr;
  bool calibrated_=false;
};
}
