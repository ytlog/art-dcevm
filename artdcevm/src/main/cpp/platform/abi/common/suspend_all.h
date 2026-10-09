#pragma once
namespace artdcevm::abi::common {
// Fixed thread_list.h 11--17: empty nonvirtual ValueObject. The named native
// constructor owns suspension/locks; the carrier contains no Thread state.
struct SuspendAll {unsigned char empty;};
static_assert(sizeof(SuspendAll)==1 && alignof(SuspendAll)==1);
inline constexpr char kSuspendConstruct[]="_ZN3art16ScopedSuspendAllC2EPKcb";
inline constexpr char kSuspendDestroy[]="_ZN3art16ScopedSuspendAllD2Ev";
using SuspendConstruct=void (*)(void*,const char*,bool);
using SuspendDestroy=void (*)(void*);
}
