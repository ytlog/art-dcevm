#pragma once
#include <jni.h>
#include <jvmti.h>
#include <string>
#include "platform/common/member_retirement.h"

// Process owner of the hooks on ART's own structural-redefinition transaction
// (docs/STRUCTURAL_REDEFINE_ENGINE.md). Installed hooks stay for the process
// lifetime.
namespace artdcevm::structural_redefine {

// Checks that the runtime was Java-debuggable at init, then installs:
//  - public-table JvmtiFunctions::RedefineClasses -> original
//    Redefiner::StructurallyRedefineClasses, after
//    reading each target's live declarations while the thread is still Native;
//  - CheckClass/CheckFields/CheckMethods -> structural_policy decisions.
// `env` is this library's own JVMTI environment; `placeholders` are the
// library's RetiredField classes used for retired fields. Idempotent.
jvmtiError Install(JavaVM* vm, jvmtiEnv* env,
                   const member_retirement::FieldPlaceholders& placeholders);

// JSON capability report. lastRejection names the class and reason of the
// latest outermost standard-entry transaction's failure, empty when it succeeded.
std::string Capabilities();

}  // namespace artdcevm::structural_redefine
