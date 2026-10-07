#pragma once
#include "base/locks.h"
#include <cstddef>
namespace art { class ArtField; }
namespace artdcevm::android13::source {
void RetireField(art::ArtField* field) REQUIRES(art::Locks::mutator_lock_);
bool IsRetiredField(art::ArtField* field) REQUIRES_SHARED(art::Locks::mutator_lock_);
}
