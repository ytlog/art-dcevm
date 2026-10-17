#pragma once
#include <cstddef>
#include <cstdint>

namespace artdcevm::abi::arm64 {
// Every offset comes from a fixed source declaration in an androidNN directory.
// This describes the fields used by the shared interface implementation.
struct InterfaceRuntime {
  size_t access_flags, class_loader, dex_cache, super_class, clinit_thread_id;
  size_t dex_class_def_index, status;
  size_t scope_header_size, scope_link, scope_capacity, scope_size;
  size_t redefinitions, initialized, actually_structural, initial_structural, redefinition_size;
  bool scope_thread_id_signed;
  static constexpr size_t kAbsent = static_cast<size_t>(-1);
};
template <class Class, class Header, class Holder, bool SignedThreadId = false>
constexpr InterfaceRuntime InterfaceRuntimeFor(
    size_t scope_size, size_t clinit_offset = offsetof(Class, clinit_thread_id)) {
  return {offsetof(Class, access_flags), offsetof(Class, class_loader),
      offsetof(Class, dex_cache), offsetof(Class, super_class),
      clinit_offset, offsetof(Class, dex_class_def_index), offsetof(Class, status),
      sizeof(Header), offsetof(Header, link),
      offsetof(Header, number_of_references), scope_size, offsetof(Holder, redefinitions), offsetof(Holder, initialized),
      offsetof(Holder, actually_structural), offsetof(Holder, initial_structural),
      sizeof(typename Holder::Redefinition), SignedThreadId};
}
}  // namespace artdcevm::abi::arm64
