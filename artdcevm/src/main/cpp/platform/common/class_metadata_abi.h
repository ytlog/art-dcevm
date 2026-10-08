#pragma once
#include "platform/abi/arm64/android11/class.h"
#include "platform/abi/arm64/android16/class.h"
#include "platform/abi/arm64/android17/class.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
namespace artdcevm::class_metadata {
// Fixed release mirror/class.h declarations, arm64 compressed references.
// The provider must match/calibrate the device declaration, keep the class
// rooted and hold the ART mutator lock. This view never decodes references.
using ObjectPrefix = abi::arm64::ObjectPrefix;
using Class11 = abi::arm64::android11::Class11;
using Class16 = abi::arm64::android16::Class16;
using Class17 = abi::arm64::android17::Class17;
static_assert(sizeof(Class11)==120 && offsetof(Class11,access_flags)==64 && offsetof(Class11,dex_class_def_index)==80);
static_assert(sizeof(Class16)==112 && offsetof(Class16,access_flags)==56 && offsetof(Class16,dex_class_def_index)==72);
static_assert(sizeof(Class17)==108 && offsetof(Class17,access_flags)==56 && offsetof(Class17,dex_class_def_index)==72);
struct Layout {size_t size,access_flags,dex_class_def_index,dex_type_index,primitive_type,status;};
inline constexpr Layout kLegacy{sizeof(Class11),offsetof(Class11,access_flags),offsetof(Class11,dex_class_def_index),offsetof(Class11,dex_type_index),offsetof(Class11,primitive_type),offsetof(Class11,status)};
inline constexpr Layout k16{sizeof(Class16),offsetof(Class16,access_flags),offsetof(Class16,dex_class_def_index),offsetof(Class16,dex_type_index),offsetof(Class16,primitive_type),offsetof(Class16,status)};
inline constexpr Layout k17{sizeof(Class17),offsetof(Class17,access_flags),offsetof(Class17,dex_class_def_index),offsetof(Class17,dex_type_index),offsetof(Class17,primitive_type),offsetof(Class17,status)};
namespace detail {
inline const Layout* CheckedDeclaration(const Layout& declaration,const void* klass,size_t extent) {
  const auto* layout=&declaration;
  const auto address=reinterpret_cast<uintptr_t>(klass);
  if(layout->size<4 || layout->access_flags>layout->size-4 ||
      layout->dex_class_def_index>layout->size-4 || layout->dex_type_index>layout->size-4 ||
      layout->primitive_type>layout->size-4 || layout->status>layout->size-4 ||
      layout->access_flags%4 || layout->status%4 || layout->size<offsetof(Class11,dex_cache)+4 ||
      !klass || extent<layout->size || address%4 ||
      address>std::numeric_limits<uintptr_t>::max()-layout->size)return nullptr;
  return layout;
}
inline uint32_t AccessFlags(const unsigned char* bytes,const Layout& layout) {
  return __atomic_load_n(reinterpret_cast<const uint32_t*>(bytes+layout.access_flags),__ATOMIC_RELAXED);
}
}
// No managed reference is read or decoded by this scalar state view.
// Status uses the source GetStatus synchronization barrier; primitive bits
// are immutable, while access flags retain atomic relaxed reads.
struct State {
  uint32_t access_flags,primitive_type,status;
  bool IsPrimitive() const { return (primitive_type & 0xffffu) != 0; }
};
inline bool ReadState(const Layout& layout,const void* klass,size_t extent,State* output) {
  const auto* checked=detail::CheckedDeclaration(layout,klass,extent);
  if(!checked || !output)return false;
  const auto* bytes=static_cast<const unsigned char*>(klass);
  State result{};
  result.access_flags=detail::AccessFlags(bytes,layout);
  std::memcpy(&result.primitive_type,bytes+layout.primitive_type,4);
  result.status=__atomic_load_n(reinterpret_cast<const uint32_t*>(bytes+layout.status),__ATOMIC_SEQ_CST)>>28;
  *output=result;return true;
}
struct Identity {uint32_t dex_cache,access_flags;int32_t class_def_index,dex_type_index;};
inline bool ReadIdentity(const Layout& layout,const void* klass,size_t extent,Identity* output) {
  const auto* checked=detail::CheckedDeclaration(layout,klass,extent);
  if(!checked || !output)return false;
  const auto* bytes=static_cast<const unsigned char*>(klass);
  Identity result{};
  std::memcpy(&result.dex_cache,bytes+offsetof(Class11,dex_cache),4);
  result.access_flags=detail::AccessFlags(bytes,layout);
  std::memcpy(&result.class_def_index,bytes+layout.dex_class_def_index,4);
  std::memcpy(&result.dex_type_index,bytes+layout.dex_type_index,4);
  *output=result;return true;
}
}
