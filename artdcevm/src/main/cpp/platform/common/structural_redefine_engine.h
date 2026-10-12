#pragma once
#include <jni.h>
#include <jvmti.h>
#include <string>

// Process owner of the hooks on ART's own structural-redefinition transaction
// (docs/STRUCTURAL_REDEFINE_ENGINE.md). Installed hooks stay for the process
// lifetime.
namespace artdcevm::structural_redefine {

// Checks that the runtime was Java-debuggable at init, then installs:
//  - public-table JvmtiFunctions::RedefineClasses -> original
//    Redefiner::StructurallyRedefineClasses, after
//    reading each target's live declarations while the thread is still Native;
//  - CheckClass/CheckFields/CheckMethods -> structural_policy decisions.
// `env` is this library's own JVMTI environment. Idempotent.
jvmtiError Install(JavaVM* vm, jvmtiEnv* env);

// JSON capability report, including the last rejection reason.
std::string Capabilities();

}  // namespace artdcevm::structural_redefine
