#pragma once
#include "api/hook_engine.h"
#include <jni.h>
#include <string>
namespace artdcevm::android13 {
bool ConfigureRuntimeAccess(HookEngine& hooks, void* implementation, std::string* error);
bool CalibrateJniIdDecode(void* implementation, JNIEnv* env, jobject calibration, std::string* error);
void* RuntimeClassLinker();
void* RuntimeInstrumentation();
void* RuntimeInstance();
void* RuntimeJniIdManager();
void* RuntimeJitCodeCache();
void* RuntimeJit();
// Callers hold ART's shared mutator lock. Pointer IDs pass through, indexed
// IDs delegate synchronization and mapping to the device JniIdManager.
void* DecodeMethodId(jmethodID id);
void* DecodeFieldId(jfieldID id);
}
