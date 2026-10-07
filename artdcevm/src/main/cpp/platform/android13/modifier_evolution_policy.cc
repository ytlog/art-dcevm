#include "platform/android13/modifier_evolution_policy.h"

namespace artdcevm::android13 {
namespace {
constexpr uint32_t kPublic = 0x0001, kPrivate = 0x0002, kProtected = 0x0004;
constexpr uint32_t kStatic = 0x0008, kFinal = 0x0010, kSynchronized = 0x0020;
constexpr uint32_t kNative = 0x0100, kAbstract = 0x0400, kDeclaredSynchronized = 0x20000;
constexpr uint32_t kVisibility = kPublic | kPrivate | kProtected;
constexpr uint32_t kMonitor = kSynchronized | kDeclaredSynchronized;
using Action = ModifierEvolutionAction;
}

ModifierEvolutionAction CheckMethodModifierEvolution(uint32_t old_flags, uint32_t new_flags,
                                                     bool structural, bool new_direct) {
  const uint32_t difference = old_flags ^ new_flags;
  if (difference == 0) return Action::kUnchanged;
  if (!structural) return Action::kReject;
  if (difference == kStatic) return Action::kRecreate;
  if ((difference & kPrivate) != 0 && (difference & ~kVisibility) == 0) {
    return Action::kRecreate;
  }
  if (((old_flags | new_flags) & kPrivate) == 0 &&
      (difference & (kPublic | kProtected)) != 0 &&
      (difference & ~(kVisibility | kMonitor)) == 0) {
    return Action::kRecreate;
  }
  if ((difference & ~kMonitor) == 0) return Action::kRecreate;
  if ((difference & kFinal) != 0 && (difference & ~(kFinal | kMonitor)) == 0) {
    return (new_flags & kFinal) != 0 && !new_direct
        ? Action::kCheckFinalOverride : Action::kRecreate;
  }
  if (difference == kAbstract && !new_direct) {
    return (new_flags & kAbstract) != 0
        ? Action::kCheckAbstractDependents : Action::kRecreate;
  }
  if (difference == kNative) return Action::kRecreate;
  return Action::kReject;
}

ModifierEvolutionAction CheckClassModifierEvolution(uint32_t old_flags, uint32_t new_flags,
                                                    bool structural) {
  const uint32_t difference = old_flags ^ new_flags;
  if (difference == 0) return Action::kUnchanged;
  if (!structural || (difference != kFinal && difference != kAbstract)) return Action::kReject;
  return (new_flags & kFinal) != 0 ? Action::kCheckLoadedSubclass : Action::kRecreate;
}
}
