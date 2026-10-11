#pragma once
#include "platform/common/bootstrap_dex_oracle.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/modern_field_metadata.h"
#include <memory>
#include <jvmti.h>
namespace artdcevm::production_bootstrap {
// Ordinary JNI input owner. Retains only SDK calibration inputs and mirrors,
// never a borrowed ART pointer, Thread or JNI environment. Destroy while this
// VM still has a current JNI attachment. Vectors are frozen before publication.
class CalibrationInputs final {
 public:
 static std::unique_ptr<CalibrationInputs>Create(JNIEnv*,jvmtiEnv*,jobject calibration,std::string*);
 ~CalibrationInputs();
 CalibrationInputs(const CalibrationInputs&)=delete;CalibrationInputs&operator=(const CalibrationInputs&)=delete;
 jobject Input()const noexcept{return input_;}
 jclass Anchor()const noexcept{return anchor_;}
 jobject Storage()const noexcept{return storage_;}
 jclass StorageClass()const noexcept{return storage_class_;}
 const std::vector<std::vector<unsigned char>>&Apk()const noexcept{return apk_;}
 const evolution::ClassSchema&MethodsSchema()const noexcept{return methods_schema_;}
 const evolution::ClassSchema&FieldsSchema()const noexcept{return fields_schema_;}
 const evolution::MappedDexMemberCounts&MethodCounts()const noexcept{return method_counts_;}
 const std::vector<method_identity::Oracle>&Methods()const noexcept{return methods_;}
 const std::vector<modern_field_metadata::FieldOracle>&Fields()const noexcept{return fields_;}
 static void*NativeMarker()noexcept;
 private:
 CalibrationInputs()=default;
 bool Load(JNIEnv*,jvmtiEnv*,jobject,std::string*);
 jobject Own(JNIEnv*,jobject);
 JavaVM*vm_=nullptr;std::vector<jobject>roots_;
 jobject input_=nullptr,storage_=nullptr;jclass anchor_=nullptr,storage_class_=nullptr;
 std::vector<std::vector<unsigned char>>apk_;
 evolution::ClassSchema methods_schema_,fields_schema_;
 evolution::MappedDexMemberCounts method_counts_{};
 std::vector<method_identity::Oracle>methods_;
 std::vector<std::string>field_names_,field_types_;
 std::vector<modern_field_metadata::FieldOracle>fields_;
};
}
