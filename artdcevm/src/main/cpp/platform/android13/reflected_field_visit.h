#pragma once
#include "api/hook_engine.h"
#include <string>
namespace artdcevm::android13 {
bool ConfigureReflectedFieldVisit(HookEngine&,void* implementation,std::string*);
// The ART caller owns exclusive mutator access and rooted/to-space Field storage.
void VisitReflectedFieldTarget(void* field,void* visitor);
}
