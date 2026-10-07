#pragma once
#include "api/hook_engine.h"
#include <cstdint>
#include <string>
namespace artdcevm::android13 {
bool ConfigureLoadedClassGuards(HookEngine&, void* class_linker, std::string* error);
bool CheckLoadedSubclassGuard(void* definition);
bool IsValidationAssignableFrom(void* target, void* source);
bool IsValidationSubclassOf(void* source, void* target);
bool CanAccessValidationClass(void* source, void* target);
bool CanAccessValidationMember(void* source,void* declaring,uint32_t flags);
void* ReadValidationMethodClass(void* method);
bool CheckMethodDependentGuard(void* definition, const char* name, const char* descriptor,
                               uint32_t old_flags, int action);
}
