#pragma once
#include <cstddef>
#include "platform/abi/arm64/common/closure.h"
namespace artdcevm::abi::arm64::android_common {
// Fixed AOSP runtime/thread_list.h declarations. Selection is by exact named
// symbol, never API/vendor identity. A receiver is independently observed.
// android-11.0.0_r1: callback defaults null.
using CheckpointCallback=size_t(*)(void*,art::Closure*,art::Closure*);
inline constexpr char kCheckpointCallback[]="_ZN3art10ThreadList13RunCheckpointEPNS_7ClosureES2_";
// android-15.0.0_r1: allow_lock_checking defaults true.
using CheckpointChecking=size_t(*)(void*,art::Closure*,art::Closure*,bool);
inline constexpr char kCheckpointChecking[]="_ZN3art10ThreadList13RunCheckpointEPNS_7ClosureES2_b";
// android-17.0.0_r1: acquire_mutator_lock defaults false.
using CheckpointMutator=size_t(*)(void*,art::Closure*,art::Closure*,bool,bool);
inline constexpr char kCheckpointMutator[]="_ZN3art10ThreadList13RunCheckpointEPNS_7ClosureES2_bb";
}
