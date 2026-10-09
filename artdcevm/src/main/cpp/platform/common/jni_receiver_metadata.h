#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
namespace artdcevm::jni_receiver_metadata {
struct Receiver {uint64_t self,vm;};
// Fixed JNIEnvExt source prefix: JNIEnv::functions followed by immutable self_
// and vm_. Function-table contents can change under CheckJNI; do not snapshot
// them. Caller must supply a live current-thread mapping and two independently
// observed non-null owner addresses. Success proves only this borrowed prefix.
inline bool ReadMatchedPrefix(size_t pointer_width,const void* environment,
                              size_t extent,uint64_t expected_self,
                              uint64_t expected_vm,Receiver* output) {
  if(pointer_width!=8 || !environment || !output ||
      !expected_self || !expected_vm || extent<3*pointer_width ||
      reinterpret_cast<uintptr_t>(environment)%pointer_width ||
      reinterpret_cast<uintptr_t>(environment)>
          std::numeric_limits<uintptr_t>::max()-3*pointer_width)
    return false;
  const auto* bytes=static_cast<const unsigned char*>(environment);
  Receiver candidate{};
  std::memcpy(&candidate.self,bytes+8,8);
  std::memcpy(&candidate.vm,bytes+16,8);
  if(candidate.self!=expected_self || candidate.vm!=expected_vm)return false;
  *output=candidate;return true;
}
}
