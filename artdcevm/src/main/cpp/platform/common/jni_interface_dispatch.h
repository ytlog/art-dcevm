#pragma once
#include <jni.h>
#include "jvmti.h"

namespace artdcevm::jni_interface_dispatch {
// The engine-owned environment has process lifetime, unlike a caller's agent.
void Configure(JavaVM* vm, jvmtiEnv* env);
// Install disabled hooks during Native preparation, before ART transaction locks.
bool Prepare();
bool Enable();
}  // namespace artdcevm::jni_interface_dispatch
