#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
namespace artdcevm::android13 {
// Once a member access change commits, resolved bytecode fields must use
// the fixed source checked resolver; JNI and previously authorized handles
// retain their own access authority. No caller access flags are rewritten.
void RequireFieldAccessChecks();
bool FieldAccessChecksRequired();
bool InstallRetiredFieldHooks(HookEngine& hooks, const DeviceProfile& profile, std::string* error);
}
