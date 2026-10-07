#pragma once
#include "api/hook_engine.h"
#include <jni.h>
#include <string>
namespace artdcevm::android13 {
void CopyEvolutionFields(bool is_static, void* target, void* target_class, void* source, void* source_class);
bool ConfigureEvolutionFieldCopy(HookEngine&, void* implementation, JNIEnv*, jobject calibration,
                                std::string* error);
}
