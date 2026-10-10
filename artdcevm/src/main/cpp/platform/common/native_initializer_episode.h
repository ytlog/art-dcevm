#pragma once
#include "platform/common/source_handle_roots.h"
namespace artdcevm::native_initializer_episode {
// The owner and its metadata/root services must outlive the installed source
// observers. Calibrate only in ordinary JNI after real source root calibration.
// Positive evidence concerns unmodified private source episodes, not samePlan
// active transfer, transaction writes, retirement or continuation permission.
class Service;
// Immutable identity of a still-active original source entry. It contains no
// cached managed pointer and cannot itself authorize mutation or retirement.
class Witness final {
 public:
  Witness()=default;
 private:
  friend class Service;
  const Service*owner_=nullptr;const void*entry_=nullptr;
  void*self_=nullptr;uint64_t epoch_=0;
};
class Service final {
 public:
  Service()=default;
  Service(const Service&)=delete;
  Service& operator=(const Service&)=delete;
  bool Calibrate(HookEngine&,JNIEnv*,jobject strong_executable_global,
      const modern_field_metadata::FieldMetadata&,const source_handle_roots::Service&,
      std::string*);
  bool UsesClassLinker(const void*)const noexcept;
  bool CaptureCurrent(JNIEnv*,Witness*)const noexcept;
  // The caller must supply its actual stopped, bounded mapping context; source
  // Handle cells remain collector-updated throughout the original episode.
  bool MatchesSourceClass(const Witness&,const modern_field_metadata::ReadContext&,
      const void* actual_class)const noexcept;
  bool ReadyFor(const modern_field_metadata::FieldMetadata&,
      const source_handle_roots::Service&)const noexcept;
};
}
