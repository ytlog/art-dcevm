#pragma once

#include "api/hook_engine.h"
#include "platform/android13/validation_metadata_abi.h"
#include <string>

namespace artdcevm::android13 {
bool ConfigureValidationMetadata(HookEngine& hooks, void* implementation, std::string* error);
// The source transaction owns the shared mutator lock. No managed pointer is
// retained beyond this call or across a JNI/JVMTI call or ART suspension point.
bool ReadValidationMetadata(void* definition, ValidationMetadata* output);
}
