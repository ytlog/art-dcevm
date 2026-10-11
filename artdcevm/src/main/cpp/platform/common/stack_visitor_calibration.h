#pragma once
#include "api/hook_engine.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/stack_visitor_service.h"
#include <jni.h>
namespace artdcevm::stack_visitor_calibration {
// Constructor storage declaration only. No walk, quick frame, inline cache,
// compiled code or transaction safety is proved by this ordinary episode.
// The independent ArtMethod model must agree with the observed declaration.
// Failure preserves output. No SDK/version or candidate field scan is used.
bool Calibrate(HookEngine&,JNIEnv*,jobject owned_global,
               const method_identity::Calibration&,stack_visitor_service::Declaration*,std::string*);
}
