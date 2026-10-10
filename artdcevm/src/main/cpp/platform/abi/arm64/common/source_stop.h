#pragma once
#include <cstdint>
namespace artdcevm::abi::arm64::android_common::source_stop {
// Fixed heap.h/gc_cause.h/collector_type.h/gc_type.h, 11--17. These values
// precede version-dependent collectors. StartGC is an exclusion operation,
// not a request to execute a mark-sweep collection; FinishGC(kNone) records none.
inline constexpr int32_t kInstrumentationCause=8,kNonMovingReservation=1,kNoCollection=0;
using StartGC=void (*)(void*,void*,int32_t,int32_t);
using FinishGC=void (*)(void*,void*,int32_t);
using MutatorOperation=void (*)(void*,void*);
inline constexpr char kStartGC[]="_ZN3art2gc4Heap7StartGCEPNS_6ThreadENS0_7GcCauseENS0_13CollectorTypeE";
inline constexpr char kFinishGC[]="_ZN3art2gc4Heap8FinishGCEPNS_6ThreadENS0_9collector6GcTypeE";
inline constexpr char kDisableMoving[]="_ZN3art2gc4Heap24IncrementDisableMovingGCEPNS_6ThreadE";
inline constexpr char kEnableMoving[]="_ZN3art2gc4Heap24DecrementDisableMovingGCEPNS_6ThreadE";
inline constexpr char kMutator[]="_ZN3art5Locks13mutator_lock_E";
inline constexpr char kLock[]="_ZN3art17ReaderWriterMutex13ExclusiveLockEPNS_6ThreadE";
inline constexpr char kUnlock[]="_ZN3art17ReaderWriterMutex15ExclusiveUnlockEPNS_6ThreadE";
inline constexpr char kCurrent[]="_ZN3art6Thread14CurrentFromGdbEv";
inline constexpr char kFinalCause[]="Final installation of redefined Classes!";
}
