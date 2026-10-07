#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include "evolution/class_schema.h"
#include <cstdint>
#include <string>
namespace artdcevm::android13 {
uint32_t BeginHierarchyTransaction();
void EndHierarchyTransaction(uint32_t previous);
bool HasHierarchyChange();
bool HasHierarchyNarrowing();
bool ConfigureHierarchyValidation(HookEngine&,const DeviceProfile& profile,std::string* error);
int CheckEvolutionHierarchy(void* definition,const evolution::ClassSchema& current,
    const evolution::ClassSchema& replacement,bool* changed,std::string* error);
}
