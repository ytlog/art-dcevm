#include "platform/android13/allocation_validation.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/redefinition_failure.h"
#include "platform/android13/validation_metadata_abi.h"
#include <cstring>
#include <dlfcn.h>
#include <limits>

namespace artdcevm::android13 {
namespace {
allocation_abi::Probe probe = nullptr;
template<class T> T Read(const void* object, size_t offset = 0) {
  T value;
  std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value));
  return value;
}
int32_t Length(void* array) { return Read<int32_t>(array, allocation_abi::kArrayLengthOffset); }
void* Element(void* array, int32_t index) {
  return ReadHeapReference(array, allocation_abi::kReferenceArrayDataOffset + size_t{4} * index);
}
void* Slot(void* array, int32_t index, int32_t slot) {
  return Element(array, allocation_abi::kSlots * index + slot);
}
bool InternalFailure(void* definition, const char* message) {
  RecordRedefinitionFailure(definition, 113 /*JVMTI_ERROR_INTERNAL*/, message);
  return false;
}
bool HasAbstractVTableMethod(void* klass) {
  // An abstract class is not instantiable and does not have an embedded vtable.
  // Its external PointerArray stores native ArtMethod* entries at 64-bit width.
  auto* vtable = ReadHeapReference(klass, offsetof(validation_abi::ClassPrefix, vtable));
  if (!vtable) return false;
  for (int32_t index = 0, count = Length(vtable); index < count; ++index) {
    auto* method = reinterpret_cast<void*>(Read<uint64_t>(vtable,
        allocation_abi::kPointerArrayDataOffset + size_t{8} * index));
    if (Read<uint32_t>(method, offsetof(art_method_abi::ArtMethod, access_flags)) &
        allocation_abi::kAbstractFlag) return true;
  }
  return false;
}
}
bool ConfigureAllocationValidation(void* implementation, std::string* error) {
  auto check = reinterpret_cast<allocation_abi::Probe>(
      dlsym(implementation, "ArtDcevmSourceProbeAllocationValidation"));
  if (!check) { *error = "Missing allocation-validation layout calibration"; return false; }
  probe = check;
  return true;
}
bool ReadAllocationState(void* data, allocation_abi::State* state) {
  using namespace allocation_abi;
  if (!data || !state) return false;
  const auto iterator = Read<Iterator>(data);
  if (iterator.index < 0 || !iterator.holder) return false;
  const auto holder = Read<Holder>(iterator.holder);
  const auto& bits = holder.initial_structural;
  if (!holder.array_handle || !bits.begin || static_cast<uint64_t>(iterator.index) >= bits.size)
    return false;
  const bool initial = (Read<uint64_t>(bits.begin, (iterator.index / 64) * sizeof(uint64_t)) &
                        (uint64_t{1} << (iterator.index % 64))) != 0;
  // Handle::Get reads its StackReference::AsMirrorPtr. StackReference uses
  // ObjectReference<false> (unpoisoned uint32_t), with no heap read barrier;
  // ART already keeps this slot rooted and updates it during a thread flip.
  auto* array = reinterpret_cast<void*>(static_cast<uintptr_t>(Read<uint32_t>(holder.array_handle)));
  if (!array || iterator.index > (std::numeric_limits<int32_t>::max() - kSlots) / kSlots ||
      Length(array) < kSlots * (iterator.index + 1)) return false;
  *state = {initial, Slot(array, iterator.index, kOldInstancesSlot),
            Slot(array, iterator.index, kOldClassesSlot), Slot(array, iterator.index, kNewClassesSlot)};
  return true;
}
bool ValidateAllocatedInstances(void* definition, void* data) {
  allocation_abi::State state{};
  if (!ReadAllocationState(data, &state))
    return InternalFailure(definition, "Invalid allocation-validation transaction layout");
  // This transition profile checker only compares owned reads with declarations;
  // all validation decisions and error semantics remain in the enhancement.
  if (!probe || !probe(data, &state))
    return InternalFailure(definition, "Allocation-validation ART declarations did not match");
  if (!state.initial_structural) return true;
  if (!state.old_instances || !state.old_classes || !state.new_classes)
    return InternalFailure(definition, "Missing allocated structural transaction roots");
  const int32_t class_count = Length(state.old_classes);
  if (class_count < 0 || class_count != Length(state.new_classes) || Length(state.old_instances) < 0)
    return InternalFailure(definition, "Invalid structural instance/class array lengths");
  for (int32_t index = 0, count = Length(state.old_instances); index < count; ++index) {
    void* instance = Element(state.old_instances, index);
    if (!instance) return InternalFailure(definition, "Missing old instance during allocation validation");
    void* old_type = ReadHeapReference(instance, 0);
    for (int32_t class_index = 0; class_index < class_count; ++class_index) {
      if (old_type != Element(state.old_classes, class_index)) continue;
      void* replacement = Element(state.new_classes, class_index);
      if (!replacement) return InternalFailure(definition, "Missing replacement class during allocation validation");
      if ((Read<uint32_t>(replacement, offsetof(validation_abi::ClassPrefix, access_flags)) &
           allocation_abi::kAbstractFlag) && HasAbstractVTableMethod(replacement)) {
        RecordRedefinitionFailure(definition, 71 /*JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED*/,
                                  "A direct instance would inherit an abstract method");
        return false;
      }
      break;
    }
  }
  return true;
}
bool CollectAndValidateInstances(bool evolution, void* definition, void* data,
                                 CollectInstances original) {
  if (!original(definition, data)) return false;
  return !evolution || ValidateAllocatedInstances(definition, data);
}
}
