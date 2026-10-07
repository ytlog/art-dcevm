#pragma once
#include "api/hook_engine.h"
#include <jni.h>
#include <string>
namespace artdcevm::android13 {
bool ConfigureEvolutionFieldCopy(HookEngine&, void* implementation, JNIEnv*, jobject calibration,
                                std::string* error);
}
