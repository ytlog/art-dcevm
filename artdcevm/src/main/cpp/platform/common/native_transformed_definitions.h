#pragma once
#include "api/runtime_backend.h"
#include <cstddef>
#include <string>
#include <vector>

namespace artdcevm::native_transformed_definitions {
// Copy the actual final source vector before Direct acquires native owners.
// The original Transformer owns the vector, JNI handles and DEX allocations
// throughout this call. This reader invokes no JNI and retains no native view;
// callers compare the copied handles only after its mapping scope has ended.
// Readability alone grants no Class identity or transaction permission.
bool Capture(const void* source,size_t expected_count,
             std::vector<Definition>*output,std::string*error);
}
