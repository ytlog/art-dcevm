#pragma once
#include "api/hook_engine.h"
#include <jni.h>
#include <jvmti.h>
namespace artdcevm::runtime_heap_receiver {
// Observe the actual Heap receiver used by this VM's public ForceGC. The caller
// owns the independently created JVMTI environment until this returns. No
// Runtime field/layout, SDK selector, GC policy change or stop grant is used.
bool Calibrate(HookEngine&,JNIEnv*,JavaVM*,jvmtiEnv*,void**heap,std::string*);
}
