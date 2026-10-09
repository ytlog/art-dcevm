#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include <jni.h>
#include <jvmti.h>
#include "platform/android13/runtime_receiver_probe.h"
#include "platform/android13/owned_jit_receiver_probe.h"
#include <string>
namespace artdcevm::android13 {
bool ConfigureRuntimeAccess(HookEngine& hooks, const DeviceProfile& profile, std::string* error,
                            const RuntimeReceivers* receivers=nullptr,
                            const OwnedJitReceivers* jit_receivers=nullptr);
void* RuntimeClassLinker();
void* RuntimeInstrumentation();
void* RuntimeInstance();
void* RuntimeJniIdManager();
void* RuntimeJitCodeCache();
void* RuntimeJit();
// Readers reject non-null service replacements without fresh owned calibration.
// A null JIT returns null and preserves the original no-JIT cache guard.
// Independent complete public Field/Class proof for both bootstrap fixtures and
// their superclass chains. Must finish before any private field consumer/hook.
bool CalibrateRuntimeFieldIds(HookEngine&,JNIEnv*,jvmtiEnv*,jobject primary,
                             jobject storage,std::string*);
// Callers hold ART's shared mutator lock. Method pointer IDs pass through;
// indexed methods and all fields delegate to the original JniIdManager.
// Fields return null until complete rooted public-field calibration succeeds;
// even public IDs are never interpreted here as native addresses.
void* DecodeMethodId(jmethodID id);
void* DecodeFieldId(jfieldID id);
}
