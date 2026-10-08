#pragma once
#include <cstddef>
#include <cstdint>
#include "platform/abi/arm64/android11/transaction_roots.h"

namespace artdcevm::android13::allocation_abi {
// android-13.0.0_r1 openjdkjvmti/ti_redefine.cc and platform libc++ vector<bool>.
// No NDK STL object is used to access platform objects.
using BitVector=abi::arm64::android11::TransactionBits;
using Holder=abi::arm64::android11::TransactionHolder;
using Iterator=abi::arm64::android11::TransactionIterator;
struct State {
  bool initial_structural;
  void* old_instances;
  void* old_classes;
  void* new_classes;
};
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
