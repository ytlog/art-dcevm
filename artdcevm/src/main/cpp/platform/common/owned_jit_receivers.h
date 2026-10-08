#pragma once
#include "api/hook_engine.h"
#include "platform/common/runtime_services.h"
#include "platform/common/method_identity_calibration.h"
namespace artdcevm::owned_jit_receivers {
// Own Class/reflected method roots, and pass the independently calibrated
// Method11/12 model. The Java fixture is a static int hotLoop(int) implementation.
// Original Java execution, public JVMTI GC and a real public native additive
// transaction on a fresh owned fixture provide native receivers. The additive
// fixture is changed on success or on a later oracle mismatch; output remains
// unchanged on failure, but the fixture mutation is not rolled back.
// Full private evolution is never used to establish its own prerequisite.
// candidate Runtime slots are never passed to a native function as receivers.
bool CalibrateJitReceivers(HookEngine&,JNIEnv*,jobject strong_class,
    const method_identity::Oracle& hot_method,
    const method_identity::Calibration&,const runtime_services::Snapshot&,
    jclass owned_additive_target,const unsigned char* additive_dex,size_t dex_size,
    runtime_services::View* output,std::string* error);
}
