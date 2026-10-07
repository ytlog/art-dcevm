#pragma once
#include <cstddef>
#include <cstdint>

namespace artdcevm::android13::allocation_abi {
// android-13.0.0_r1 openjdkjvmti/ti_redefine.cc and platform libc++ vector<bool>.
// No NDK STL object is used to access platform objects.
struct BitVector { const uint64_t* begin; uint64_t size; uint64_t capacity; };
struct Holder {
  const uint32_t* array_handle;
  void* redefinitions;
  BitVector initialized;
  BitVector actually_structural;
  BitVector initial_structural;
};
struct Iterator { int32_t index; Holder* holder; };
struct State {
  bool initial_structural;
  void* old_instances;
  void* old_classes;
  void* new_classes;
};
using Probe = bool (*)(void*, const State*);
inline constexpr int32_t kSlots = 13;
inline constexpr int32_t kOldInstancesSlot = 9;
inline constexpr int32_t kOldClassesSlot = 11;
inline constexpr int32_t kNewClassesSlot = 12;
inline constexpr size_t kArrayLengthOffset = 8;
inline constexpr size_t kReferenceArrayDataOffset = 12;
inline constexpr size_t kPointerArrayDataOffset = 16;
inline constexpr uint32_t kAbstractFlag = 0x400;
static_assert(sizeof(BitVector) == 24);
static_assert(sizeof(Holder) == 88 && offsetof(Holder, initial_structural) == 64);
static_assert(sizeof(Iterator) == 16 && offsetof(Iterator, holder) == 8);
}
