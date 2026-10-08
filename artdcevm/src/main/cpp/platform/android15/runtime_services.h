#pragma once
#include "api/hook_engine.h"
#include "platform/common/runtime_services.h"
#include <jni.h>
namespace artdcevm::android15 {
struct RuntimeServices {
  void* runtime=nullptr;
  void* instrumentation=nullptr;
  // Only independently observed receivers are published. JIT/cache/heap stay
  // null until their own public-operation oracle establishes identity.
  runtime_services::View receivers;
};
// Fixed-source candidate validated against public JNI and JVMTI observations.
// JVMTI agent must already be attached. Borrowed services remain owned by ART;
// this grants no class/Thread layout token.
bool CalibrateRuntimeServices(HookEngine&,JNIEnv*,RuntimeServices*,std::string* error);
}
