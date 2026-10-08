#pragma once
#include "platform/common/modern_field_metadata.h"
#include "platform/common/exclusive_owned_roots.h"
namespace artdcevm::modern_field_storage {
// Calibrate with an initialized, exclusively owned fixture class/instance and
// the complete independent reflection oracle. Static mutations are restored.
// Candidate source card/monitor operations are unavailable until all real JNI
// value, non-dirty card transition and public monitor oracles have succeeded.
class Service {
 public:
  bool Calibrate(HookEngine&,JNIEnv*,jobject strong_class,jobject strong_fixture,
      const modern_field_metadata::FieldMetadata&,const modern_field_metadata::FieldOracle*,size_t,std::string*);
  bool Calibrated()const{return calibrated_;}
  size_t CardTableOffset()const{return calibrated_?card_offset_:0;}
 private:
  modern_field_metadata::FieldMetadata metadata_;
  exclusive_owned_roots::Service exclusive_;
  abi::arm64::android11::CurrentThread current_=nullptr;
  size_t card_offset_=0;
  bool calibrated_=false;
  friend class ScopedAccess;
  friend struct Adapter;
  friend struct Calibrator;
};
// Select an immutable calibrated service for common FieldCopyAccess callbacks
// on this owning thread. Only use under an ART-owned exclusive commit scope.
class ScopedAccess {
 public:
  explicit ScopedAccess(const Service&);
  ~ScopedAccess();
  field_copy::FieldCopyAccess Access()const;
  ScopedAccess(const ScopedAccess&)=delete;
  ScopedAccess& operator=(const ScopedAccess&)=delete;
 private:
  const Service* previous_;
};
}
