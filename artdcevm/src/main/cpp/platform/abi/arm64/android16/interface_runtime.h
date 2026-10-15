#pragma once
#include "platform/abi/arm64/common/interface_runtime.h"
#include "platform/abi/arm64/android16/class.h"
#include "platform/abi/arm64/android16/thread.h"
#include "platform/abi/arm64/android16/redefinition.h"
namespace artdcevm::abi::arm64::android16 {
inline constexpr InterfaceRuntime kInterfaceRuntime =
    InterfaceRuntimeFor<Class, Thread, HandleScopeHeader, RedefinitionDataHolder>(offsetof(HandleScopeHeader, size));
}  // namespace artdcevm::abi::arm64::android16
