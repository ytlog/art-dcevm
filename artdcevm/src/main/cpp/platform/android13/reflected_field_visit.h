#pragma once
#include "platform/android13/device_profile.h"
#include "api/hook_engine.h"
#include <string>
namespace artdcevm::android13 {
bool ConfigureReflectedFieldVisit(HookEngine&,const DeviceProfile& profile,std::string*);
// The ART caller owns exclusive mutator access and rooted/to-space Field storage.
void VisitReflectedFieldTarget(void* field,void* visitor);
}
