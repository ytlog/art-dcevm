#pragma once
#include "platform/common/source_handle_roots.h"
namespace artdcevm::native_initializer_episode {
// The owner and its metadata/root services must outlive the installed source
// observers. Calibrate only in ordinary JNI after real source root calibration.
// Positive evidence concerns unmodified private source episodes, not samePlan
// active transfer, transaction writes, retirement or continuation permission.
class Service final {
 public:
  Service()=default;
  Service(const Service&)=delete;
  Service& operator=(const Service&)=delete;
  bool Calibrate(HookEngine&,JNIEnv*,jobject strong_executable_global,
      const modern_field_metadata::FieldMetadata&,const source_handle_roots::Service&,
      std::string*);
  bool ReadyFor(const modern_field_metadata::FieldMetadata&,
      const source_handle_roots::Service&)const noexcept;
};
}
