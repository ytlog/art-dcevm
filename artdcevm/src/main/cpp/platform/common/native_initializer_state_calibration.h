#pragma once
#include "platform/common/modern_field_metadata.h"
namespace artdcevm::native_initializer_state {
enum class TidStorage {kDedicated,kDescriptorHashShared};
// Dedicated source slots retain their tid after completion. Only status 13
// denotes active initialization; a nonzero getter alone never proves it.
struct State {uint32_t status=0,initializing_tid=0,raw_slot=0;};
// Genuine private public <clinit> observation calibrates the fixed state/tid
// declaration, same-thread reentry/default cell and one-body error poisoning.
// This is not a migration, waiter, frame or retirement authority.
class Calibration {
 public:
  bool Calibrate(JNIEnv*,jobject strong_executable_global,
      const modern_field_metadata::FieldMetadata&,std::string*);
  bool CalibratedFor(const modern_field_metadata::FieldMetadata&)const noexcept;
  bool Read(const modern_field_metadata::FieldMetadata&,const void* rooted_class,
      const modern_field_metadata::ReadContext&,State*)const noexcept;
  TidStorage Storage()const{return storage_;}
 private:
  const modern_field_metadata::FieldMetadata* metadata_=nullptr;
  TidStorage storage_=TidStorage::kDedicated;
};
}
