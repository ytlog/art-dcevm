#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android11 {
// Fixed ART RedefinitionDataHolder/Iter (11--17) and platform libc++ storage.
// Platform STL storage is borrowed only: never construct/destroy an NDK STL proxy.
struct TransactionBits { const uint64_t* begin; uint64_t size, capacity; };
struct TransactionOwners { const unsigned char* begin; const unsigned char* end; const unsigned char* capacity; };
struct TransactionHolder {
  const uint32_t* array_handle; void* redefinitions;
  TransactionBits initialized, actually_structural, initial_structural;
};
struct TransactionIterator { int32_t index; TransactionHolder* holder; };
inline constexpr size_t kRootSlots=13;
inline constexpr size_t kCacheSlot=3,kMirrorSlot=4,kOldInstancesSlot=9,kNewInstancesSlot=10,kOldClassesSlot=11,kNewClassesSlot=12;
static_assert(sizeof(void*)==8 && sizeof(TransactionBits)==24 && sizeof(TransactionOwners)==24);
static_assert(sizeof(TransactionHolder)==88 && offsetof(TransactionHolder,initialized)==16 && offsetof(TransactionHolder,initial_structural)==64);
static_assert(sizeof(TransactionIterator)==16 && offsetof(TransactionIterator,holder)==8);
}
