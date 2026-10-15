#pragma once
#include <cstddef>
#include <cstdint>

namespace artdcevm::abi::arm64 {
// Every offset comes from a fixed source declaration in an androidNN directory.
// This describes the fields used by the shared interface implementation.
struct InterfaceRuntime {
  size_t access_flags, class_loader, dex_cache, super_class, clinit_thread_id;
  size_t dex_class_def_index, status;
  size_t tid, jni_env, is_gc_marking, exception, top_handle_scope;
  size_t scope_header_size, scope_link, scope_capacity, scope_size;
  size_t redefinitions, initialized, actually_structural, initial_structural;
  static constexpr size_t kAbsent = static_cast<size_t>(-1);
};
template <class Class, class Thread, class Header, class Holder>
constexpr InterfaceRuntime InterfaceRuntimeFor(size_t scope_size) {
  return {offsetof(Class, access_flags), offsetof(Class, class_loader),
      offsetof(Class, dex_cache), offsetof(Class, super_class),
      offsetof(Class, clinit_thread_id), offsetof(Class, dex_class_def_index), offsetof(Class, status),
      offsetof(Thread, tls32.tid), offsetof(Thread, tls_ptr.jni_env),
      offsetof(Thread, tls32.is_gc_marking), offsetof(Thread, tls_ptr.exception),
      offsetof(Thread, tls_ptr.top_handle_scope), sizeof(Header), offsetof(Header, link),
      offsetof(Header, number_of_references), scope_size, offsetof(Holder, redefinitions), offsetof(Holder, initialized),
      offsetof(Holder, actually_structural), offsetof(Holder, initial_structural)};
}
}  // namespace artdcevm::abi::arm64
