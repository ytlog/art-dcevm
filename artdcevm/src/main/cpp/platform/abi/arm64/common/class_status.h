#pragma once
#include <cstdint>

namespace artdcevm::abi::arm64 {
// Identical fixed runtime/class_status.h declarations in android-13.0.0_r1
// and android-16.0.0_r1. These are ART states, not JVMTI class-status masks.
enum class ClassStatus : uint8_t {
  kNotReady = 0,
  kRetired = 1,
  kErrorResolved = 2,
  kErrorUnresolved = 3,
  kIdx = 4,
  kLoaded = 5,
  kResolving = 6,
  kResolved = 7,
  kVerifying = 8,
  kRetryVerificationAtRuntime = 9,
  kVerifiedNeedsAccessChecks = 10,
  kVerified = 11,
  kSuperclassValidated = 12,
  kInitializing = 13,
  kInitialized = 14,
  kVisiblyInitialized = 15,
  kLast = kVisiblyInitialized
};
// mirror/class.h: ClassStatus occupies the high four bits of the status word.
inline constexpr uint32_t kClassStatusShift = 28;
static_assert(sizeof(ClassStatus) == 1);
}  // namespace artdcevm::abi::arm64
