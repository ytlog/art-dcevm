#pragma once
#include "platform/abi/arm64/common/interface_runtime.h"
#include "platform/abi/arm64/android13/class.h"
#include "platform/abi/arm64/android13/thread.h"
#include "platform/abi/arm64/android13/redefinition.h"
namespace artdcevm::abi::arm64::android13 {
inline constexpr InterfaceRuntime kInterfaceRuntime =
    InterfaceRuntimeFor<Class, HandleScopeHeader, RedefinitionDataHolder>(InterfaceRuntime::kAbsent);
}  // namespace artdcevm::abi::arm64::android13
