#pragma once
#include <cstdint>

namespace artdcevm::android13 {
using ProbeFieldValidationLayout = int (*)();
using FieldValidationFailure = void (*)(void*, int, const char*);
using MarkFinalFieldGuard = void (*)();
using FieldValidation = bool (*)(void*);
using ConfigureFieldValidation = void (*)(FieldValidation);
}
