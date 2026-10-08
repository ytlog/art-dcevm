#pragma once
#ifdef ARTDCEVM_TESTING
#include "platform/common/modern_method_services.h"
#include "platform/common/modern_field_storage.h"
#include "platform/common/executable_mirror_calibration.h"
#include "platform/common/runtime_services.h"
#include "platform/common/retired_method_registry.h"
#include "platform/common/retired_field_registry.h"
namespace artdcevm::jni_access {
// Debug fixture bootstrap retains only independently calibrated native
// capabilities. No borrowed managed object pointer is retained here.
struct EvolutionProofs {
 std::unique_ptr<modern_methods::Binding> methods;
 std::unique_ptr<executable_mirror::Layout> mirrors;
 method_identity::Calibration method_model;
 modern_field_metadata::FieldMetadata metadata;
 modern_field_storage::Service fields;
 runtime_services::Snapshot observed;
};
EvolutionProofs& OwnedEvolutionProofs();
retirement::RetiredMethodRegistry& OwnedMethodRegistry();
retirement::RetiredFieldRegistry& OwnedFieldRegistry();
void CaptureFieldProofs(const modern_field_metadata::FieldMetadata&,const modern_field_storage::Service&);
}
#endif
