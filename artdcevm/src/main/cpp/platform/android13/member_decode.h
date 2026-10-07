#pragma once

#include "api/hook_engine.h"
#include <jni.h>
#include <string>

namespace artdcevm::android13 {
bool ConfigureMemberDecode(HookEngine& hooks, void* implementation, JNIEnv* jni,
                           jobject calibration, std::string* error);
// The caller holds the mutator lock; reflected_field is a rooted to-space
// mirror::Field. Retired tokens resolve through the enhancement-owned registry.
void* DecodeReflectedField(void* reflected_field);
// access is the existing ART ScopedObjectAccessAlreadyRunnable reference. Its
// device function owns JNI-handle decoding and the fixed Executable accessor.
void* DecodeRunnableExecutable(const void* access, jobject executable);
bool GuardRunnableExecutable(const void* access, jobject executable);
}
