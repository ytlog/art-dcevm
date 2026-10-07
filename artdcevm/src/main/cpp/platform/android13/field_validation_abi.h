#pragma once
#include <cstdint>

namespace artdcevm::android13 {
using FieldValidationFailure = void (*)(void*, int, const char*);
using ProbeFieldValidationLayout = int (*)(FieldValidationFailure);
using FieldValidation = bool (*)(void*);
using ConfigureFieldValidation = void (*)(FieldValidation);
}
