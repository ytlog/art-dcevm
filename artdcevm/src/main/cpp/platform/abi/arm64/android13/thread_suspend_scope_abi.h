#pragma once
#include "platform/abi/arm64/android13/native_object_access_abi.h"
#include "platform/abi/common/suspend_all.h"
namespace artdcevm::android13::thread_suspend_abi {
// runtime/thread_list.h ScopedSuspendAll is an empty nonvirtual ValueObject;
// the named ctor/dtor use Runtime::Current rather than their this storage.
using SuspendAll=abi::common::SuspendAll;
static_assert(sizeof(SuspendAll)==1 && alignof(SuspendAll)==1);
inline constexpr const char* kConstruct=abi::common::kSuspendConstruct;
inline constexpr const char* kDestroy=abi::common::kSuspendDestroy;
}
