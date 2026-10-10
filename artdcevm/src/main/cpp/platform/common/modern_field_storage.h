#pragma once
#include "platform/common/modern_field_metadata.h"
#include "platform/common/exclusive_owned_roots.h"
namespace artdcevm::source_handle_roots { class Pool; class StoppedReader; }
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
  modern_field_metadata::Model StorageModel()const{return metadata_.StorageModel();}
  size_t CardTableOffset()const{return calibrated_?card_offset_:0;}
  // Borrowed references and mutations require the transaction owner's actual
  // exclusive mutator/disabled-GC scope. No roots escape that lifetime.
  // Runnable and stopped callers use separate root readers. Neither API
  // grants private-cache ownership or publication permission.
  bool SourceBarrier(const runnable_jni::Context&,const source_handle_roots::Pool&,const void* root)const noexcept;
  // Requires the actual exclusive reader and its fresh preallocated mapping
  // context; performs no JNI, mapping query, allocation or safepoint.
  bool SourceBarrierStopped(const runnable_jni::Context&,const source_handle_roots::Pool&,
      const source_handle_roots::StoppedReader&,const void* root,
      const modern_field_metadata::ReadContext*)const noexcept;
  void* ObjectClass(void*)const;
  void CopyFields(bool is_static,void* target,void* target_class,
                  void* source,void* source_class,const modern_field_metadata::ReadContext* =nullptr)const;
  void RestoreOldFieldDeclaringClass(void* old_class,const modern_field_metadata::ReadContext* =nullptr)const;
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
  field_copy::FieldCopyAccess Access(const modern_field_metadata::ReadContext* =nullptr)const;
  ScopedAccess(const ScopedAccess&)=delete;
  ScopedAccess& operator=(const ScopedAccess&)=delete;
 private:
  const Service* previous_;
};
}
