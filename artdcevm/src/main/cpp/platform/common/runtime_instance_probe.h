#pragma once
#include "api/hook_engine.h"
#include <jni.h>
namespace artdcevm::runtime_instance {
// Observe the original Runtime receiver reached through the supplied public VM.
// Does not read Runtime or JavaVMExt storage or authorize any private layout.
bool Calibrate(HookEngine&,JavaVM*,void**,std::string*);
}
