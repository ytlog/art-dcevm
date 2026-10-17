#pragma once
#include <cstdint>

namespace artdcevm::abi::arm64 {
// Fixed android-11.0.0_r1--android-17.0.0_r1 jni/jni_id_manager.cc:
// IndexToId produces odd IDs; DecodeGenericId treats even IDs as pointers.
template <class Id> bool IsIndexedJniId(Id id) {
  return (reinterpret_cast<uintptr_t>(id) & 1u) != 0;
}
}  // namespace artdcevm::abi::arm64
