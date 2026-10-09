#pragma once
#include "platform/common/native_schema_policy.h"
#include "platform/common/native_method_table.h"
#include "platform/common/modern_method_services.h"
#include "platform/common/source_commit.h"
namespace artdcevm::native_schema_policy {
// Complete declarations with exact flags/indexes and named GcRoot provenance.
// Optional table output lets transaction owners recheck native array identities.
bool ReadNativeDeclarations(const modern_field_metadata::FieldMetadata&,const method_identity::Calibration&,
 void* klass,evolution::ClassSchema*,std::string*,native_method_table::View* table=nullptr);
// Exact current native flags, storage, DEX identity and loader/Class graph must
// agree with every supplied policy. Default null guard refuses required proofs.
bool ValidateNative(const source_commit::Plan&,const modern_field_metadata::FieldMetadata&,
 const method_identity::Calibration&,const std::vector<const modern_methods::Service*>&,
 const std::vector<Plan>&,const GuardVerifier*,std::string*);
}
