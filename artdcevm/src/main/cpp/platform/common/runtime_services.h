#pragma once
#include "platform/abi/arm64/android11/runtime.h"
#include "platform/abi/arm64/android12/runtime.h"
#include "platform/abi/arm64/android13/runtime.h"
#include "platform/abi/arm64/android14/runtime.h"
#include "platform/abi/arm64/android15/runtime.h"
#include <cstring>
#include <limits>
namespace artdcevm::runtime_services {
// Explicit fixed source declaration: API alone is not a Mainline/vendor ABI key.
enum class Declaration {kAndroid11,kAndroid12,kAndroid13,kAndroid14,kAndroid15To17};
struct View {
  void* class_linker=nullptr;
  void* jni_id_manager=nullptr;
  void* jit=nullptr;
  void* jit_code_cache=nullptr;
  void* thread_list=nullptr;
  void* heap=nullptr;
  void* java_vm=nullptr;
};
namespace detail {
template<class Prefix> bool Read(const void* runtime,size_t extent,View* output) {
  const auto address=reinterpret_cast<uintptr_t>(runtime);
  if(!runtime || !output || address%alignof(Prefix) || extent<sizeof(Prefix) ||
     sizeof(Prefix)>std::numeric_limits<uintptr_t>::max()-address)return false;
  // Read only service slots, never a whole live Runtime prefix (roots, atomic
  // pointers and platform containers can change independently). Caller holds the
  // original runtime/mutator/lifetime locks and has calibrated these declarations.
  auto pointer=[&](size_t offset){
    void* value;
    std::memcpy(&value,static_cast<const unsigned char*>(runtime)+offset,sizeof(value));
    return value;
  };
  View view;
  view.class_linker=pointer(offsetof(Prefix,class_linker));
  view.jni_id_manager=pointer(offsetof(Prefix,jni_id_manager));
  view.jit=pointer(offsetof(Prefix,jit));
  view.jit_code_cache=view.jit?pointer(offsetof(Prefix,jit_code_cache)):nullptr;
  view.thread_list=pointer(offsetof(Prefix,thread_list));
  view.heap=pointer(offsetof(Prefix,heap));
  view.java_vm=pointer(offsetof(Prefix,java_vm));
  *output=view;return true;
}
}
// Arithmetic bounds do not prove mapped-memory extent. Service pointers are
// borrowed, may be null, and are not an atomic coherent snapshot. No GC roots,
// NDK containers, startup atomic state or Instrumentation storage are accessed.
inline bool Read(Declaration declaration,const void* runtime,size_t extent,View* output) {
  switch(declaration) {
    case Declaration::kAndroid11:return detail::Read<abi::arm64::android11::RuntimePrefix>(runtime,extent,output);
    case Declaration::kAndroid12:return detail::Read<abi::arm64::android12::RuntimePrefix>(runtime,extent,output);
    case Declaration::kAndroid13:return detail::Read<abi::arm64::android13::RuntimePrefix>(runtime,extent,output);
    case Declaration::kAndroid14:return detail::Read<abi::arm64::android14::RuntimePrefix>(runtime,extent,output);
    case Declaration::kAndroid15To17:return detail::Read<abi::arm64::android15::RuntimePrefix>(runtime,extent,output);
  }
  return false;
}
}
