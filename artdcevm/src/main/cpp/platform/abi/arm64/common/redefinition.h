#pragma once
#include <cstddef>
#include <cstdint>
#include <jni.h>
#include <string>
#include "platform/abi/arm64/common/runtime_objects.h"

// Fixed release arm64 declarations shared by Android 11--17, checked against
// openjdkjvmti/ti_redefine.h and libdexfile/dex/dex_file.h of android-11.0.0_r1,
// android-13.0.0_r1, android-15.0.0_r1, android16-release and main. Only the
// leading members are declared; nothing here constructs or destroys ART objects.
namespace artdcevm::abi::arm64 {

// art::DexFile: vptr, begin_, size_ (unused_size_ since 15, kept for layout),
// then data_ (begin/size). The file size is read from the DEX header instead.
struct DexFilePrefix {
  const void* vtable;
  const uint8_t* begin;
  size_t size_or_unused;
  const uint8_t* data_begin;
  size_t data_size;
};
static_assert(offsetof(DexFilePrefix, begin) == 8 && offsetof(DexFilePrefix, data_begin) == 24 &&
              offsetof(DexFilePrefix, data_size) == 32);

// openjdkjvmti::Redefiner, a non-polymorphic class: env_, result_, ...
// RecordFailure only formats *error_msg_ and stores result_.
// runtime_/self_ follow result_ in android-13.0.0_r1 and android-16.0.0_r1
// ti_redefine.h; the API 33/36 interface engine reads these trailing members.
struct RedefinerPrefix {
  void* env;
  int32_t result;
  void* runtime;
  void* self;
};
static_assert(offsetof(RedefinerPrefix, result) == 8 && offsetof(RedefinerPrefix, self) == 24);

// Redefiner::ClassRedefinition: driver_, klass_, unique_ptr<const DexFile> dex_file_, ...
struct ClassRedefinitionPrefix {
  RedefinerPrefix* driver;
  jclass klass;
  const DexFilePrefix* dex_file;
};
static_assert(offsetof(ClassRedefinitionPrefix, klass) == 8 &&
              offsetof(ClassRedefinitionPrefix, dex_file) == 16);

// Full element layout, checked against android-13.0.0_r1 and
// android-16.0.0_r1 ti_redefine.h. Used only to validate/traverse the private
// holder vector; never construct, mutate or destroy its owning members.
template <size_t FlagCount> struct ClassRedefinitionLayout {
  RedefinerPrefix* driver;
  jclass klass;
  const DexFilePrefix* dex_file;
  std::string class_sig;
  struct { const unsigned char* data; size_t size; } original_dex_file;
  bool flags[FlagCount];
};
static_assert(sizeof(std::string) == 24);

// RedefinitionDataHolder wraps one ObjectArray through its single Handle;
// RedefinitionDataIter holds idx_ then RedefinitionDataHolder&. The DataSlot
// numbers are identical in android-11.0.0_r1 through android-17.0.0_r1.
struct RedefinitionDataHolderPrefix { Handle array; };
struct RedefinitionDataIter {
  int32_t index;
  RedefinitionDataHolderPrefix* holder;
  static constexpr int32_t kNumSlots = 13;
  static constexpr int32_t kSlotMirrorClass = 4;
  static constexpr int32_t kSlotNewClassObject = 8;
  static constexpr int32_t kSlotOldInstanceObjects = 9;
  static constexpr int32_t kSlotNewInstanceObjects = 10;
  static constexpr int32_t kSlotOldClasses = 11;
  static constexpr int32_t kSlotNewClasses = 12;
  ObjectArray* Slot(int32_t slot) const {
    return holder->array.Get<ObjectArray>()->Get<ObjectArray>(kNumSlots * index + slot);
  }
  ObjectArray* Classes(bool old) const { return Slot(old ? kSlotOldClasses : kSlotNewClasses); }
  // The compressed reference in `slot`, for a read barrier.
  uint32_t Reference(int32_t slot) const {
    return holder->array.Get<ObjectArray>()->elements[kNumSlots * index + slot];
  }
  // The redefined class's replacement, set by FinishNewClassAllocations.
  void* NewClassObject() const {
    return holder->array.Get<ObjectArray>()->Get<void>(kNumSlots * index + kSlotNewClassObject);
  }
  // Instances of the old classes collected by CollectAndCreateNewInstances.
  ObjectArray* OldInstances() const { return Slot(kSlotOldInstanceObjects); }
};
static_assert(offsetof(RedefinitionDataIter, holder) == 8);

// DexFile::Header::file_size_.
inline constexpr size_t kDexHeaderFileSizeOffset = 0x20;

}  // namespace artdcevm::abi::arm64
