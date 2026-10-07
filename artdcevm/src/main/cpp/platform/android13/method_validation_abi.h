#pragma once
#include <cstdint>
#include "evolution/class_schema.h"
namespace artdcevm::android13 {
using MethodValidation = bool (*)(void*);
using ConfigureMethodValidation = void (*)(MethodValidation);
}
