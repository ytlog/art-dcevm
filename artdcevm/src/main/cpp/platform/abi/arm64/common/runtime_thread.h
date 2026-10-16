#pragma once
#include <cstddef>

namespace artdcevm::abi::arm64 {
// Offsets derived only from the fixed androidNN Thread declarations.
struct RuntimeThread {
  size_t tid, jni_env, is_gc_marking, exception, top_handle_scope;
};
template <class Thread> constexpr RuntimeThread RuntimeThreadFor() {
  return {offsetof(Thread, tls32.tid), offsetof(Thread, tls_ptr.jni_env),
      offsetof(Thread, tls32.is_gc_marking), offsetof(Thread, tls_ptr.exception),
      offsetof(Thread, tls_ptr.top_handle_scope)};
}
}  // namespace artdcevm::abi::arm64
