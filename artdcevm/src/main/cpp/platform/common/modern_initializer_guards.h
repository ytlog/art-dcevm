#pragma once
#include "platform/common/modern_method_guards.h"
namespace artdcevm::retirement {
// Process-lifetime extension of the same persistent method registry. Installing
// entries alone does not prove initializer removal; the ordinary public producer
// below must observe its actual constructor paths, and transaction state/frame
// proofs remain independently required.
bool InstallInitializerEntryGuards(HookEngine&,JNIEnv*,jobject owned_executable_global,
    void* observed_jni_id_manager,RetiredMethodRegistry&,std::string*);
bool CalibrateInitializerEntryGuards(JNIEnv*,RetiredMethodRegistry&,std::string*);
bool InitializerEntryGuardsOwn(const RetiredMethodRegistry&);
}
