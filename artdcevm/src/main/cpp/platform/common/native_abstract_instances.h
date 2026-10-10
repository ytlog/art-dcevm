#pragma once
#include "platform/common/modern_field_metadata.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/source_commit.h"
namespace artdcevm::native_abstract_instances {
// Source ordinary abstract Classes have external long[] vtables. Bootstrap
// independently proves one abstract and one complete public method/slot using
// original reflection/Class identities. No JNI runs in final validation.
struct VTableView {
 void* table=nullptr;size_t payload=0,count=0;
 // Observation of mapped source storage, not an authority or owned GC root.
};
class Service {
 public:
  bool Calibrate(HookEngine&,JNIEnv*,const modern_field_metadata::FieldMetadata&,
                 const method_identity::Calibration&,std::string*);
  bool Validate(const source_commit::Plan&,size_t class_pair,
                const modern_field_metadata::FieldMetadata&,std::string*,
                const modern_field_metadata::ReadContext* =nullptr)const;
  bool ReadVTable(void* klass,const modern_field_metadata::FieldMetadata&,
                  VTableView*,std::string*,const modern_field_metadata::ReadContext*)const;
  bool Ready()const{return metadata_!=nullptr;}
 private:
  const modern_field_metadata::FieldMetadata* metadata_=nullptr;
  size_t embedded_prefix_=0;
  method_identity::Model method_model_=method_identity::Model::kMethod12Onward;
};
}
