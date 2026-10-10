#pragma once
#include "platform/abi/arm64/common/handle_roots.h"
#include <atomic>
namespace artdcevm::abi::arm64::runtime_root_visit {
// Reuse the independently calibrated common RootVisitor/RootInfo declaration;
// do not introduce another visitor layout. Fixed 16 runtime.h exports this
// signature. All/Precise values are from the fixed gc_root.h common declaration.
// Fixed AOSP commit ed6c006bd06ae060bd9698fd2cb25c4865512ec3 supplies
// gc_root.h/base/mutex.h/base/locks.h. Binding is availability only; an
// independent public-root/runtime episode is still required before permission.
using Visitor=android_common::HandleRootVisitor;
using Info=android_common::NativeRootInfo;
using Visit=void(*)(void*,Visitor*,uint8_t);
inline constexpr uint8_t kAllPrecise=1u|128u;
inline constexpr uint8_t kGenericBottom=8;
inline constexpr char kVisit[]="_ZN3art7Runtime10VisitRootsEPNS_11RootVisitorENS_14VisitRootFlagsE";
inline constexpr char kMutexOwner[]="_ZNK3art5Mutex20GetExclusiveOwnerTidEv";
inline constexpr char kRwOwner[]="_ZNK3art17ReaderWriterMutex20GetExclusiveOwnerTidEv";
inline constexpr char kShared[]="_ZNK3art17ReaderWriterMutex12IsSharedHeldEPKNS_6ThreadE";
inline constexpr char kClassLock[]="_ZN3art5Locks25classlinker_classes_lock_E";
inline constexpr char kTraceLock[]="_ZN3art5Locks11trace_lock_E";
// Complete fixed futex/no-contention-log declarations from manifest 16
// runtime/base/mutex.h. Actual inheritance is essential: derived atomic members
// reuse BaseMutex tail padding. Do not embed BaseMutex as a plain member.
struct BaseMutexPrefix {virtual ~BaseMutexPrefix()=default;const char* name;uint8_t level;bool respond;};
struct MutexPrefix:BaseMutexPrefix {std::atomic<int32_t> state,owner;unsigned recursion;const bool recursive=false;bool timeout=false;uint32_t monitor_id;};
struct RwPrefix:BaseMutexPrefix {std::atomic<int32_t> state,owner,contenders;};
using MutexConstruct=void(*)(void*,const char*,uint8_t,bool);
using RwConstruct=void(*)(void*,const char*,uint8_t);
using Destroy=void(*)(void*);using Lock=void(*)(void*,void*);
inline constexpr char kMutexConstruct[]="_ZN3art5MutexC2EPKcNS_9LockLevelEb";
inline constexpr char kMutexDestroy[]="_ZN3art5MutexD2Ev";
inline constexpr char kMutexLock[]="_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE";
inline constexpr char kMutexUnlock[]="_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE";
inline constexpr char kRwConstruct[]="_ZN3art17ReaderWriterMutexC2EPKcNS_9LockLevelE";
inline constexpr char kRwDestroy[]="_ZN3art17ReaderWriterMutexD2Ev";
inline constexpr char kRwLock[]="_ZN3art17ReaderWriterMutex13ExclusiveLockEPNS_6ThreadE";
inline constexpr char kRwUnlock[]="_ZN3art17ReaderWriterMutex15ExclusiveUnlockEPNS_6ThreadE";
inline constexpr char kTid[]="_ZN3art6GetTidEv";
}
