#pragma once
#include <jni.h>
#include <jvmti.h>
#include "evolution/class_schema.h"

namespace artdcevm::method_handle_lookup {
// Serialized Native interface-batch path only: prove public stack/local access and boot identities.
bool Prepare(JNIEnv* jni, jvmtiEnv* env);
bool Available();
// Lazy installation before admitting a symbolic interface change; no managed calls.
bool Enable();
}  // namespace artdcevm::method_handle_lookup
