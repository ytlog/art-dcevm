#pragma once
#include "platform/abi/arm64/common/interface_runtime.h"
#include "platform/abi/arm64/common/class_status.h"
#include "platform/abi/arm64/android15/class.h"
#include "platform/abi/arm64/android15/thread.h"
#include "platform/abi/arm64/android15/redefinition.h"

namespace artdcevm::abi::arm64::android15 {
// Android 15 uses a sized HandleScope and separate Class field arrays.
inline constexpr InterfaceRuntime kInterfaceRuntime =
    InterfaceRuntimeFor<Class, HandleScopeHeader, RedefinitionDataHolder>(offsetof(HandleScopeHeader, size));
inline constexpr bool kHandleScopeThreadIdSigned = false;
}  // namespace artdcevm::abi::arm64::android15
