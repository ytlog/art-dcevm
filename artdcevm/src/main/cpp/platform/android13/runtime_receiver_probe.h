#pragma once
#include "api/hook_engine.h"
#include <jni.h>
namespace artdcevm::android13 {
struct RuntimeReceivers {
  void* class_linker=nullptr;
  void* jni_id_manager=nullptr;
  JavaVM* java_vm=nullptr;
  void* thread_list=nullptr;
  void* instrumentation=nullptr;
};
// Observe receivers of real public JNI/JVMTI requests, before reading Runtime storage.
// The common service calibration owns the sole process-lifetime listener Hook.
// Only its independently observed receivers are published; the source backend
// still separately validates its complete fixed Runtime declaration.
bool CalibrateRuntimeReceivers(HookEngine& hooks,JNIEnv* env,RuntimeReceivers* output,
                              std::string* error);
}
