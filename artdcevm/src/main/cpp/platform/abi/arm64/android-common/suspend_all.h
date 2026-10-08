#pragma once
namespace artdcevm::abi::arm64::android_common {
// Fixed thread_list.h/cc 11–17: empty nonvirtual ValueObject. C2/D2 obtain the
// Runtime thread list themselves; no this-object data or padding is decoded.
struct SuspendAll {unsigned char empty;};
static_assert(sizeof(SuspendAll)==1 && alignof(SuspendAll)==1);
inline constexpr char kSuspendConstruct[]="_ZN3art16ScopedSuspendAllC2EPKcb";
inline constexpr char kSuspendDestroy[]="_ZN3art16ScopedSuspendAllD2Ev";
using SuspendConstruct=void (*)(void*,const char*,bool);
using SuspendDestroy=void (*)(void*);
}
