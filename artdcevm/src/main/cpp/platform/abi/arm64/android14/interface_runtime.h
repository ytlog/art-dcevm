#pragma once
#include "platform/abi/arm64/common/interface_runtime.h"
#include "platform/abi/arm64/common/class_status.h"
#include "platform/abi/arm64/android14/class.h"
#include "platform/abi/arm64/android14/thread.h"
#include "platform/abi/arm64/android14/redefinition.h"

namespace artdcevm::abi::arm64::android14 {
// Android 14 uses the packed HandleScope header without a size field.
inline constexpr InterfaceRuntime kInterfaceRuntime =
    InterfaceRuntimeFor<Class, HandleScopeHeader, RedefinitionDataHolder>(InterfaceRuntime::kAbsent);
inline constexpr bool kHandleScopeThreadIdSigned = false;
}  // namespace artdcevm::abi::arm64::android14
