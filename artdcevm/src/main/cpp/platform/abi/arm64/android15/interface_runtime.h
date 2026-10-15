#pragma once
#include "platform/abi/arm64/common/interface_runtime.h"
#include "platform/abi/arm64/common/class_status.h"
#include "platform/abi/arm64/android15/class.h"
#include "platform/abi/arm64/android15/thread.h"
#include "platform/abi/arm64/android15/redefinition.h"

namespace artdcevm::abi::arm64::android15 {
// Source preparation only. RuntimeFor deliberately does not select this profile.
inline constexpr InterfaceRuntime kPreparedInterfaceRuntime =
    InterfaceRuntimeFor<Class, Thread, HandleScopeHeader, RedefinitionDataHolder>(offsetof(HandleScopeHeader, size));
inline constexpr bool kHandleScopeThreadIdSigned = false;
}  // namespace artdcevm::abi::arm64::android15
