#pragma once
#include <cstdint>

namespace artdcevm::android13 {
// Borrowed DEX strings are valid only during the visitor invocation. No C++
// objects or containers cross the transitional source/NDK boundary.
using VisitFieldDeclaration = void (*)(void*, bool, const char*, const char*, uint32_t);
using ReadFieldDeclarations = bool (*)(void*, VisitFieldDeclaration, void*);
using FieldValidationFailure = void (*)(void*, int, const char*);
using MarkFinalFieldGuard = void (*)();
using FieldValidation = bool (*)(void*);
using ConfigureFieldValidation = void (*)(FieldValidation);
}
