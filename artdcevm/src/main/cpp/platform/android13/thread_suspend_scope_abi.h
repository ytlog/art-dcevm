#pragma once
#include "platform/android13/native_object_access_abi.h"
namespace artdcevm::android13::thread_suspend_abi {
// runtime/thread_list.h ScopedSuspendAll is an empty nonvirtual ValueObject;
// the named ctor/dtor use Runtime::Current rather than their this storage.
struct SuspendAll { unsigned char empty; };
static_assert(sizeof(SuspendAll)==1 && alignof(SuspendAll)==1);
inline constexpr char kConstruct[]="_ZN3art16ScopedSuspendAllC2EPKcb";
inline constexpr char kDestroy[]="_ZN3art16ScopedSuspendAllD2Ev";
}
