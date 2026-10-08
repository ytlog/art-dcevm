#pragma once
#include "platform/android13/native_object_access_abi.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
namespace artdcevm::android13::thread_suspend_abi {
// runtime/thread_list.h ScopedSuspendAll is an empty nonvirtual ValueObject;
// the named ctor/dtor use Runtime::Current rather than their this storage.
using SuspendAll=abi::arm64::android_common::SuspendAll;
static_assert(sizeof(SuspendAll)==1 && alignof(SuspendAll)==1);
inline constexpr const char* kConstruct=abi::arm64::android_common::kSuspendConstruct;
inline constexpr const char* kDestroy=abi::arm64::android_common::kSuspendDestroy;
}
