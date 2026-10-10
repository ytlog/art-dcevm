#pragma once
#include "platform/abi/arm64/android13/redefinition_art_abi.h"
#include "evolution/class_schema.h"
#include <string>
namespace artdcevm::android13 {
// Shared fixed-ABI input path for all three enhancement validation proxies.
// Allocation failures propagate to the proxy's JVMTI error boundary.
int ReadRedefinitionInput(void* definition, redefinition_abi::ClassRedefinition* metadata,
                          evolution::ClassSchema* replacement, std::string* error);
}
