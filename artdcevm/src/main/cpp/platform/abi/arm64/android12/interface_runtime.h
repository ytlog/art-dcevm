#pragma once
#include "platform/abi/arm64/common/interface_runtime.h"
#include "platform/abi/arm64/common/class_status.h"
#include "platform/abi/arm64/android12/class.h"
#include "platform/abi/arm64/android12/thread.h"
#include "platform/abi/arm64/android12/redefinition.h"

namespace artdcevm::abi::arm64::android12 {
// object_array.h also declares Alloc(Thread*, ObjPtr<Class>, int32_t, gc::AllocatorType).
// AllocatorType is an int-sized enum; the selector must come from ART, never a guess.
using ArrayAllocWithAllocator = ObjectArray* (*)(void*, void*, int32_t, int32_t);
// Source preparation only. RuntimeFor deliberately does not select this profile.
inline constexpr InterfaceRuntime kPreparedInterfaceRuntime =
    InterfaceRuntimeFor<Class, Thread, HandleScopeHeader, RedefinitionDataHolder>(InterfaceRuntime::kAbsent);
inline constexpr bool kHandleScopeThreadIdSigned = false;
}  // namespace artdcevm::abi::arm64::android12
