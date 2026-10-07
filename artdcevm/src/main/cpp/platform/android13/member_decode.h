#pragma once

#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <jni.h>
#include <string>

namespace artdcevm::android13 {
bool ConfigureMemberDecode(HookEngine& hooks, const DeviceProfile& profile, std::string* error);
// The caller holds the mutator lock; reflected_field is a rooted to-space
// mirror::Field. Retired tokens resolve through the enhancement-owned registry.
void* DecodeReflectedField(void* reflected_field);
// access is the existing ART ScopedObjectAccessAlreadyRunnable reference. Its
// device function owns JNI-handle decoding and the fixed Executable accessor.
void* DecodeRunnableExecutable(const void* access, jobject executable);
bool GuardRunnableExecutable(const void* access, jobject executable);
// Installed on calibrated FAST_NATIVE reflection entrypoints. Native/runnable
// JNI ID entrypoints below share the same owned object-access scope.
bool GuardNativeExecutable(JNIEnv* env, jobject executable);
bool GuardNativeReflectedField(JNIEnv* env, jobject field);
bool GuardRunnableMethodId(jmethodID method);
bool GuardNativeMethodId(JNIEnv* env, jmethodID method);
bool GuardRunnableFieldId(jfieldID field);
bool GuardNativeFieldId(JNIEnv* env, jfieldID field);
}
