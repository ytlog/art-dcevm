#pragma once
#include <cstdint>
namespace artdcevm::android13 {
inline constexpr uint32_t kValidationClassFlags = 0x5431;
inline constexpr uint32_t kValidationInterfaceFlags = 0x3601;
using ClassSubclassGuard = bool (*)(void*);
using ClassHierarchyGuard = int (*)(void*, bool*, const char**);
using ClassValidation = bool (*)(void*);
using ConfigureClassValidation = void (*)(ClassValidation);
}
