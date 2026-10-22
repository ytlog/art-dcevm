#pragma once
#include <cstring>
#include "evolution/class_schema.h"
namespace artdcevm::method_identity {
// art_method-inl.h GetDeclaringClassDescriptor, Android 11--13: use the
// MethodId owner in that method's own DEX, including obsolete copied defaults.
// Class::GetDescriptor is not an equivalent source for a copied method.
inline bool DeclaringDescriptor(const evolution::DexView& dex, uint32_t index,
                               std::string* descriptor) {
  if (!dex.begin || dex.size < 0x70) return false;
  uint32_t count, offset;
  std::memcpy(&count, dex.begin + 88, sizeof(count));
  std::memcpy(&offset, dex.begin + 92, sizeof(offset));
  const uint64_t at = uint64_t{offset} + uint64_t{index} * 8;
  if (index >= count || at + 8 > dex.size) return false;
  uint16_t owner;
  std::memcpy(&owner, dex.begin + at, sizeof(owner));
  std::string error;
  return evolution::ReadTypeDescriptor(dex, owner, descriptor, &error);
}
inline bool Matches(const std::string& descriptor, uintptr_t loader,
                    const char* expected, uintptr_t expected_loader) {
  return expected && descriptor == expected && loader == expected_loader;
}
}
