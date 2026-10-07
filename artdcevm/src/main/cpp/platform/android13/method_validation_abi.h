#pragma once
#include <cstdint>
#include "evolution/class_schema.h"
namespace artdcevm::android13 {
using MethodDependentGuard = bool (*)(void*, const char*, const char*, uint32_t, int);
using MethodValidation = bool (*)(void*);
using ConfigureMethodValidation = void (*)(MethodValidation);
}
