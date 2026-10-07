#pragma once
#include "api/hook_engine.h"
#include <string>
namespace artdcevm::android13 {
bool ConfigureRedefinitionFailures(HookEngine& hooks, std::string* error);
void RecordRedefinitionFailure(void* definition, int code, const char* message);
}
