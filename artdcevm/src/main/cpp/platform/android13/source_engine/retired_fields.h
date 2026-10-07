#pragma once
#include "base/locks.h"
#include <cstddef>
namespace art { class ArtField; class LinearAlloc; class Thread; union JValue; }
namespace artdcevm::android13::source {
void RetireField(art::ArtField* field) REQUIRES(art::Locks::mutator_lock_);
bool IsRetiredField(art::ArtField* field) REQUIRES_SHARED(art::Locks::mutator_lock_);
bool GuardField(art::ArtField* field, art::Thread* thread, art::JValue* result = nullptr)
    REQUIRES_SHARED(art::Locks::mutator_lock_);
void MarkFinalFieldAddition();
size_t RetiredFieldCount() REQUIRES_SHARED(art::Locks::mutator_lock_);
size_t PruneRetiredFields(const art::LinearAlloc* allocator)
    REQUIRES_SHARED(art::Locks::mutator_lock_);
}
