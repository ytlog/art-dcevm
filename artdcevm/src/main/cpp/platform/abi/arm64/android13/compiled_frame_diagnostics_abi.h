#pragma once
#include <cstdint>
namespace artdcevm::android13::compiled_frame_abi {
// android-13.0.0_r1 runtime/suspend_reason.h; arm64 release profile.
enum class SuspendReason:char { kInternal=0 };
inline constexpr char kSuspend[]="_ZN3art10ThreadList19SuspendThreadByPeerEP8_jobjectNS_13SuspendReasonEPb";
inline constexpr char kResume[]="_ZN3art10ThreadList6ResumeEPNS_6ThreadENS_13SuspendReasonE";
// Symtab-local accessor retains the device's optimized CodeInfo/code-size policy.
inline constexpr char kContains[]="_ZNK3art20OatQuickMethodHeader8ContainsEm";
static_assert(sizeof(SuspendReason)==1);
}
