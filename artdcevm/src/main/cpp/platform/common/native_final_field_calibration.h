#pragma once
#include "platform/common/native_final_field_service.h"
#include "platform/common/runnable_jni_bridge.h"
namespace artdcevm::final_field_calibration {
struct Bindings {
 void* owner=nullptr;
 final_field_service::Service* service=nullptr;
 HookEngine* hooks=nullptr;
 void* observed_instrumentation=nullptr;
 // Actual calibrated JNI Runnable scope, and original source-owned exclusive
 // suspend/mutator scope. Neither callback may synthesize a Context or merely
 // call an operation without acquiring its stated source locks.
 bool (*native_scope)(void*,JNIEnv*,jobject,runnable_jni::Operation,void*)=nullptr;
 bool (*exclusive_scope)(void*,JNIEnv*,jobject,runnable_jni::Operation,void*)=nullptr;
};
// Installed APK fixtures, actual bytecode event + independently read stores,
// legal JNI/accessible reflection, cached MH/VH final setter/getter semantics.
// Caller publishes no final/access owner unless this entire operation succeeds.
// No global deoptimization is performed to obtain calibration events.
bool Run(JNIEnv*,const Bindings&,std::string*);
}
