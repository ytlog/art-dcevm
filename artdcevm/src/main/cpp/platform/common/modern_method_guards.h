#pragma once
#include "api/hook_engine.h"
#include "platform/common/retired_method_registry.h"
#include <jni.h>
#include <string>

namespace artdcevm::retirement {
// Process-lifetime guard owner. The registry and observed JniIdManager must
// outlive every installed hook. Initialize only after owned Runnable calibration.
// This covers named reflective/JNI/interpreter invocation entries; it does not
// claim nterp resolution, MethodHandle dispatch, or compiled caller invalidation.
bool InstallMethodEntryGuards(HookEngine&, JNIEnv*, jobject owned_strong_global,
                             void* observed_jni_id_manager,
                             RetiredMethodRegistry&, std::string* error);
size_t RejectedMethodEntries();
// Shared owner queries for calibrated native MethodHandle dispatch guards.
bool MethodEntryGuardsOwn(const RetiredMethodRegistry&);
bool RejectRetiredMethod(void* method,void* self,void* result);
}  // namespace artdcevm::retirement
