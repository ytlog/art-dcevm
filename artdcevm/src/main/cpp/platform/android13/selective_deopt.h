#pragma once
#include "api/hook_engine.h"
#include <string>
namespace artdcevm::android13 {
bool ConfigureSelectiveDeopt(HookEngine&,void* source,std::string*);
void DeoptimizeApplicationFrames();
}
