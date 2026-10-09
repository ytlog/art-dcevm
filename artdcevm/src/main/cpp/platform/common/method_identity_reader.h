#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
namespace artdcevm::method_identity_reader {
struct Identity {uint32_t access_flags, dex_method_index;};
// Layout and mapping lifetime are supplied by an independently calibrated
// provider. This reader does not select a declaration by SDK, vendor or BuildID.
// It never snapshots declaring_class (requires ART read barriers), hotness or
// mutable entry/data pointers. Access flags use ART's relaxed atomic semantics.
inline uint32_t ReadAccessFlags(const void* method) {
  const auto* flags=reinterpret_cast<const uint32_t*>(
      static_cast<const unsigned char*>(method)+4);
  return __atomic_load_n(flags,__ATOMIC_RELAXED);
}
inline bool ReadIdentity(size_t size,size_t dex_index_offset,const void* method,
                         size_t extent,Identity* output) {
  if(size<8 || dex_index_offset>size-4 || !method || !output || extent<size ||
      reinterpret_cast<uintptr_t>(method)%alignof(uint32_t) ||
      reinterpret_cast<uintptr_t>(method)>std::numeric_limits<uintptr_t>::max()-size)
    return false;
  uint32_t index;
  std::memcpy(&index,static_cast<const unsigned char*>(method)+dex_index_offset,4);
  *output={ReadAccessFlags(method),index};
  return true;
}
}
