#pragma once
#include <jni.h>
#include <jvmti.h>
#include <string>
#include <vector>
#include "platform/common/art_hook.h"

// The engine's member mapping on ART's structural transaction: the plan made
// before ART's no-failure point (CollectAndCreateNewInstances) from the layout
// that class_layout selected, and its commit (CollectNewFieldAndMethodMappings,
// UpdateClassStructurally). Each target's transaction::Requirements say what
// the plan must enforce.
namespace artdcevm::member_mapping {
// Resolve the mapping, verification-redirect and retirement-guard hooks by
// name, then install the JNI guards through JVMTI's function-table override.
// When a name is missing, the NDK map layout differs or the override fails, no
// hook is added, members cannot retire and `note` says why. Whether the fixed
// declarations fit this ART is decided later by probes.
void Prepare(JavaVM* vm, jvmtiEnv* jvmti, std::vector<Hook>* hooks, std::string* note);
// The hooks are prepared; class_layout::VerifyTarget decides per transaction.
bool Available();
// The verifier redirect for superclass changes is installed.
bool SuperclassChangeAvailable();
}  // namespace artdcevm::member_mapping
