#pragma once
#include <cstdint>
#include <string_view>
#include "platform/abi/arm64/common/runtime_objects.h"

namespace artdcevm::class_layout {

// Class.java declares the native field-array slots as long instance fields.
// Ask ART for that declaration before dereferencing a candidate array. In
// particular, an empty array has no named ArtField that could witness its base.
template <typename FindField>
bool ProbeFieldArray(void* class_type, std::string_view name, uint32_t offset,
                     uint64_t array, FindField find, uint32_t* length) {
  using namespace artdcevm::abi::arm64;
  const ArtField* slot = find(class_type, name, "J");
  if (!slot || slot->declaring_class != reinterpret_cast<uintptr_t>(class_type) ||
      (slot->access_flags & 0x8u) || slot->offset != offset)
    return false;
  *length = array ? reinterpret_cast<const LengthPrefixedArray<ArtField>*>(
                        static_cast<uintptr_t>(array))->length : 0;
  return true;
}
}  // namespace artdcevm::class_layout
