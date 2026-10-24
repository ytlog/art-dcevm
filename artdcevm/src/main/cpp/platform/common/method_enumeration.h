#pragma once
#include <jni.h>
#include <jvmti.h>

namespace artdcevm::method_enumeration {
// Serialized Native interface-batch phase, before ART's transaction locks.
bool Prepare(JNIEnv* jni, jvmtiEnv* environment);
bool Enable();
}
