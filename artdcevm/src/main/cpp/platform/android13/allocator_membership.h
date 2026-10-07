#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <jni.h>
namespace artdcevm::android13 {
using AllocatorContains = bool (*)(const void*, void*);
// Requires completed owned bootstrap. Returned callback does not acquire locks.
bool ConfigureAllocatorMembership(HookEngine&, const DeviceProfile&, JNIEnv*, jobject,
                                  AllocatorContains*, std::string*, bool force_arena = false);
}
