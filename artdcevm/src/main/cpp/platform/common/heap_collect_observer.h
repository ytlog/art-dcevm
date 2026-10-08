#pragma once
#include "api/hook_engine.h"
#include "platform/abi/arm64/android-common/gc_cause.h"
namespace artdcevm::heap_collect_observer {
// Fixed heap.h: GcCause has a 32-bit enum representation. Observers are static functions
// with process lifetime; they filter their own owned request before recording.
// No borrowed stack/context pointer is retained by the shared owner.
using Cause=art::gc::GcCause;
using Observer=void (*)(void* heap,bool clear_soft_references,Cause cause);
inline constexpr char kCollect[]="_ZN3art2gc4Heap14CollectGarbageEbNS0_7GcCauseE";
bool Install(HookEngine&,Observer,std::string* error);
}
