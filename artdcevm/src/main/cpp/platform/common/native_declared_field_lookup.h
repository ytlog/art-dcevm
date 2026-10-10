#pragma once
#include "platform/common/modern_field_metadata.h"
#include "platform/common/original_field_declaration.h"
namespace artdcevm::native_declared_field_lookup {
// Diagnostic counts, never a permission or declaration witness.
struct Observation {size_t mapping_os_queries=0,inspected_fields=0;};
// Caller has independently entered its actual rooted Runnable/source read
// interval. `klass` is freshly decoded there, not a retained JNI-era address.
// Read current VMA bounds only for this immediate traversal; no JNI/factory/safepoint
// may occur within it. Every physical selected member is checked, including
// duplicate declarations, before publishing one exact native identity.
void* Read(const modern_field_metadata::FieldMetadata&,void* current_self,
           void* (*current)(),void* klass,
           const original_field_declaration::Declaration&,std::string*,
           Observation* =nullptr);
}
