#pragma once
#include "platform/common/production_calibration_inputs.h"
#include "platform/common/runtime_service_calibration.h"
#include "platform/common/field_mirror_calibration.h"
#include "platform/common/executable_mirror_calibration.h"
#include "platform/common/modern_method_services.h"
#include "platform/common/modern_field_storage.h"
namespace artdcevm::production_bootstrap {
// Long-lived immutable metadata services and their independently retained SDK
// oracles. Create performs ordinary public calibration; it is not an Engine,
// execution proof, source-stop calibration or permission to apply a schema.
class MetadataCalibration final {
 public:
 static std::unique_ptr<MetadataCalibration>Create(HookEngine&,JNIEnv*,jvmtiEnv*,jobject,std::string*);
 MetadataCalibration(const MetadataCalibration&)=delete;MetadataCalibration&operator=(const MetadataCalibration&)=delete;
 const CalibrationInputs&Inputs()const noexcept{return *inputs_;}
 const runtime_services::Snapshot&Runtime()const noexcept{return runtime_;}
 const method_identity::Calibration&Methods()const noexcept{return methods_;}
 const modern_methods::Binding&MethodBinding()const noexcept{return *binding_;}
 const modern_field_metadata::FieldMetadata&Fields()const noexcept{return fields_;}
 const modern_field_storage::Service&Storage()const noexcept{return storage_;}
 const field_mirror::Calibration&FieldMirrors()const noexcept{return field_mirrors_;}
 const executable_mirror::Layout&ExecutableMirrors()const noexcept{return *mirrors_;}
 private:
 MetadataCalibration()=default;
 struct Request;
 static bool Observe(const runnable_jni::Context&,void*);
 bool Prepare(HookEngine&,JNIEnv*,jvmtiEnv*,jobject,std::string*);
 // Declared first, destroyed last: every oracle outlives its native service.
 std::unique_ptr<CalibrationInputs>inputs_;
 runtime_services::Snapshot runtime_{};
 method_identity::Calibration methods_{};
 modern_field_metadata::FieldMetadata fields_;
 modern_field_storage::Service storage_;
 field_mirror::Calibration field_mirrors_{};
 std::unique_ptr<executable_mirror::Layout>mirrors_;
 std::unique_ptr<modern_methods::Binding>binding_;
};
}
