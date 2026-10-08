#pragma once
#include "api/hook_engine.h"
#include <jni.h>
namespace artdcevm::android17 {
struct RuntimeReceivers {
  void* class_linker=nullptr;
  void* jni_id_manager=nullptr;
  JavaVM* java_vm=nullptr;
  void* thread_list=nullptr;
};
// Observe receivers of real public JNI requests, before reading Runtime storage.
// Hooks remain installed as passthroughs for process lifetime. No object prefix,
// SDK number or ELF identity is used to produce these independent observations.
bool CalibrateRuntimeReceivers(HookEngine& hooks,JNIEnv* env,RuntimeReceivers* output,
                              std::string* error);
// Reuse the process-lifetime FindClass hook for evolution. Setting a handler
// returns the original platform trampoline; nullptr restores passthrough.
// Returns false if this observer has not installed its hook.
bool SetRuntimeFindClassHandler(void* handler,void** original);
}
