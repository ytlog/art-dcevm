#pragma once
#include "platform/common/native_schema_policy.h"
namespace artdcevm::native_schema_policy {
// This pure declaration plan does not grant a capability. The transaction must
// verify every requirement against current rooted ART Classes/instances before
// touching methods, and ART still verifies all incoming method bodies.
// Input uses Java flags (including normalized declared-synchronized).
// Failure preserves the caller's requirement vector.
bool PrepareMethodEvolution(const evolution::ClassSchema& before,
    const evolution::ClassSchema& after,std::vector<Requirement>*,std::string*);
}
