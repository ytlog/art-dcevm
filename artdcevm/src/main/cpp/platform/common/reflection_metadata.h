#pragma once
#include "platform/abi/arm64/android11/reflection.h"
#include "platform/abi/arm64/android12/reflection.h"
#include <cstddef>
#include <cstring>
namespace artdcevm::reflection_metadata {
using Field11 = abi::arm64::android11::ReflectedField;
using Field12 = abi::arm64::android12::ReflectedField;
using Executable11 = abi::arm64::android11::Executable;
using Executable12 = abi::arm64::android12::Executable;
static_assert(sizeof(Field11) == 32 && sizeof(Field12) == 32);
static_assert(sizeof(Executable11) == 40 && sizeof(Executable12) == 40);
static_assert(offsetof(Field11, declaring_class) == 12 && offsetof(Field12, declaring_class) == 12);
static_assert(offsetof(Executable11, has_real_parameter_data) == 10 && offsetof(Executable12, has_real_parameter_data) == 9);
struct Layout {
  size_t field_size, executable_size, field_flags, field_index, field_offset, art_method;
};
inline constexpr Layout k11{sizeof(Field11), sizeof(Executable11), offsetof(Field11,access_flags),
    offsetof(Field11,art_field_index), offsetof(Field11,offset), offsetof(Executable11,art_method)};
inline constexpr Layout k12{sizeof(Field12), sizeof(Executable12), offsetof(Field12,access_flags),
    offsetof(Field12,art_field_index), offsetof(Field12,offset), offsetof(Executable12,art_method)};
inline const Layout* Declaration(int api) {
  if (api == 30) return &k11;
  return api >= 31 && api <= 37 ? &k12 : nullptr;
}
inline bool Valid(const void* mirror, size_t extent, size_t size) {
  const auto address = reinterpret_cast<uintptr_t>(mirror);
  return mirror && extent >= size && address % 4 == 0 && address <= UINTPTR_MAX - size;
}
struct FieldIdentity {uint32_t access_flags, array_index, offset;};
// Rooted live mirrors and calibrated declarations are caller prerequisites.
// Read scalar metadata only; managed declaring-class/type references require
// the provider's read barrier. Preserve private retired-token index bits.
inline bool ReadField(const Layout& declaration, const void* mirror, size_t extent, FieldIdentity* output) {
  const auto* layout = &declaration;
  if (!output || !Valid(mirror,extent,layout->field_size)) return false;
  const auto* bytes = static_cast<const unsigned char*>(mirror);
  FieldIdentity result;
  std::memcpy(&result.access_flags,bytes+layout->field_flags,4);
  std::memcpy(&result.array_index,bytes+layout->field_index,4);
  std::memcpy(&result.offset,bytes+layout->field_offset,4);
  *output = result; return true;
}
inline bool ReadField(int api, const void* mirror, size_t extent, FieldIdentity* output) {
  const auto* layout=Declaration(api);
  return layout&&ReadField(*layout,mirror,extent,output);
}
// Null is a valid stored native method, including serialization constructors.
// No dereference of the extracted native method or unrooted managed reference.
inline bool ReadExecutableMethod(int api, const void* mirror, size_t extent, void** output) {
  const auto* layout = Declaration(api);
  if (!layout || !output || !Valid(mirror,extent,layout->executable_size)) return false;
  uint64_t method;
  std::memcpy(&method,static_cast<const unsigned char*>(mirror)+layout->art_method,8);
  *output = reinterpret_cast<void*>(static_cast<uintptr_t>(method)); return true;
}
}
