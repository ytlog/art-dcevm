#pragma once
#include "api/hook_engine.h"
#include <jni.h>
namespace artdcevm::runtime_receiver {
struct Receivers {
  void* class_linker=nullptr;
  void* jni_id_manager=nullptr;
  JavaVM* java_vm=nullptr;
  void* thread_list=nullptr;
};
// Original public JNI and SuspendAll supply receivers of the named c-string
// FindClass declaration. No SDK, Runtime storage or Instrumentation ABI is used.
// Missing/inlined/conflicting calls fail without changing output. Hooks remain
// process-lifetime passthroughs; version providers own additional observations.
bool Calibrate(HookEngine&,JNIEnv*,Receivers*,std::string* error);
bool SetFindClassHandler(void* handler,void** original);
}
