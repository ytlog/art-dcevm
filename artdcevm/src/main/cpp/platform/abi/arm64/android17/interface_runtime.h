#pragma once
#include "platform/abi/arm64/common/interface_runtime.h"
#include "platform/abi/arm64/common/class_status.h"
#include "platform/abi/arm64/android17/class.h"
#include "platform/abi/arm64/android17/thread.h"
#include "platform/abi/arm64/android17/redefinition.h"

namespace artdcevm::abi::arm64::android17 {
// Source-aligned with the stored runtimes; not yet device-validated.
inline constexpr InterfaceRuntime kInterfaceRuntime =
    InterfaceRuntimeFor<Class, HandleScopeHeader, RedefinitionDataHolder>(offsetof(HandleScopeHeader, size),
        offsetof(Class, clinit_thread_id_or_hash));
inline constexpr bool kHandleScopeThreadIdSigned = false;
}  // namespace artdcevm::abi::arm64::android17
