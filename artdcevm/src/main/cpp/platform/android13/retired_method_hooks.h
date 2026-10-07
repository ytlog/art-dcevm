#pragma once
#include "api/hook_engine.h"
#include <jni.h>
namespace artdcevm::android13 {
bool InstallRetiredMethodHooks(HookEngine& hooks, std::string* error);
int RejectedRetiredInvocations();
#ifdef ARTDCEVM_TESTING
int SourceCheckedHandleDispatches();
bool ProbeCheckedHandleHook(JNIEnv*);
#endif
}
