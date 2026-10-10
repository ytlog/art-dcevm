#include "platform/android13/allocation_validation.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/redefinition_failure.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include <cstring>
#include "platform/android13/transaction_metadata.h"
#include <limits>

namespace artdcevm::android13 {
namespace {
template<class T> T Read(const void* object, size_t offset = 0) {
  T value;
  std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value));
  return value;
}
int32_t Length(void* array) { return Read<int32_t>(array, allocation_abi::kArrayLengthOffset); }
void* Element(void* array, int32_t index) {
  return ReadHeapReference(array, allocation_abi::kReferenceArrayDataOffset + size_t{4} * index);
}
bool InternalFailure(void* definition, const char* message) {
  RecordRedefinitionFailure(definition, 113 /*JVMTI_ERROR_INTERNAL*/, message);
  return false;
}
}
bool ConfigureAllocationValidation(const DeviceProfile& profile, std::string* error) {
  if(!profile.Matched()){*error="Allocation validation requires a matched device profile";return false;}
  if(GetTransactionMetadataReader().Configured())return true;
  if(error)*error="Transaction metadata reader must be configured before allocation validation";
  return false;
}
bool ReadAllocationState(void* data, allocation_abi::State* state) {
  TransactionRootView roots;std::string error;
  if(!state || !GetTransactionMetadataReader().ReadRoots(data,&roots,&error)) return false;
  *state={roots.initial_structural,roots.old_instances,roots.old_classes,roots.new_classes};return true;
}
bool ValidateAllocatedInstances(void* definition, void* data) {
  allocation_abi::State state{};
  if (!ReadAllocationState(data, &state))
    return InternalFailure(definition, "Invalid allocation-validation transaction layout");
  if (!state.initial_structural) return true;
  if (!state.old_instances || !state.old_classes || !state.new_classes)
    return InternalFailure(definition, "Missing allocated structural transaction roots");
  const int32_t class_count = Length(state.old_classes);
  if (class_count < 0 || class_count != Length(state.new_classes) || Length(state.old_instances) < 0)
    return InternalFailure(definition, "Invalid structural instance/class array lengths");
  // Validate every abstract replacement's external table, including classes
  // that currently have no directly allocated instance.
  for(int32_t i=0;i<class_count;++i) {
    bool abstract=false;std::string error;
    if(!GetTransactionMetadataReader().ReadAbstractVTable(Element(state.new_classes,i),&abstract,&error))
      return InternalFailure(definition,"Invalid replacement abstract vtable");
  }
  for (int32_t index = 0, count = Length(state.old_instances); index < count; ++index) {
    void* instance = Element(state.old_instances, index);
    if (!instance) return InternalFailure(definition, "Missing old instance during allocation validation");
    void* old_type = ReadHeapReference(instance, 0);
    for (int32_t class_index = 0; class_index < class_count; ++class_index) {
      if (old_type != Element(state.old_classes, class_index)) continue;
      void* replacement = Element(state.new_classes, class_index);
      if (!replacement) return InternalFailure(definition, "Missing replacement class during allocation validation");
      bool abstract=false;std::string error;
      if(!GetTransactionMetadataReader().ReadAbstractVTable(replacement,&abstract,&error))
        return InternalFailure(definition,"Invalid replacement abstract vtable");
      if (abstract) {
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
