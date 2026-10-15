#pragma once
#include <atomic>
#include <cstdint>
#include <jni.h>
#include <jvmti.h>
#include <string>
#include <vector>
#include "evolution/class_schema.h"
#include "platform/common/art_hook.h"
#include "platform/abi/arm64/common/redefinition.h"

namespace artdcevm::method_retirement {
// Resolve the retirement hooks by name and install the JNI Call*Method* guards
// through JVMTI's function-table override. When a name is missing, the NDK map
// layout differs or the override fails, no hook is added, members cannot retire
// and `note` says why. Whether the fixed declarations fit this ART is
// decided later by probes.
void Prepare(JavaVM* vm, jvmtiEnv* jvmti, std::vector<Hook>* hooks, std::string* note);
// The retirement hooks are prepared; VerifyTarget decides per transaction.
bool Available();
// The verifier redirect for superclass changes is installed.
bool SuperclassChangeAvailable();
}  // namespace artdcevm::method_retirement
