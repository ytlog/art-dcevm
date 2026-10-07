#pragma once
#include "base/locks.h"
#include <cstddef>
namespace art { class ArtMethod; class ReflectionSourceInfo; }
namespace artdcevm::android13::source {
// Metadata addresses are native pointers obtained from ART objects, never derived offsets.
// Readers hold the shared mutator lock; structural commit writes under the exclusive lock.
void RetireMethod(art::ArtMethod* method) REQUIRES(art::Locks::mutator_lock_);
bool IsRetiredMethod(art::ArtMethod* method) REQUIRES_SHARED(art::Locks::mutator_lock_);
void UpdateReflectedMethodModifiers(const art::ReflectionSourceInfo&, art::ArtMethod*)
    REQUIRES(art::Locks::mutator_lock_);
void RequireDispatchChecks() REQUIRES(art::Locks::mutator_lock_);
}
