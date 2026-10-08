#pragma once
#include "platform/abi/arm64/android16/thread.h"
#include "platform/abi/arm64/android17/thread.h"
namespace artdcevm::thread_refresh {
// This is source provenance, never permission to read an unmatched Thread.
enum class SourceDeclaration { kAndroid16, kAndroid17 };
struct SourceLayout { size_t gc_marking_offset; size_t suspend_trigger_offset; size_t prefix_extent; };
template<class T> constexpr SourceLayout LayoutOf() {
  return {offsetof(T, tls32_) + offsetof(decltype(T::tls32_), is_gc_marking),
          offsetof(T, tlsPtr_) + offsetof(decltype(T::tlsPtr_), suspend_trigger), sizeof(T)};
}
// Caller must select an independently calibrated source declaration, libart
// identity, ARM64 ABI and read-barrier configuration. Source alone cannot
// create a runtime calibration token. In particular Android13 is not this model.
constexpr bool SourceLayoutFor(SourceDeclaration declaration, SourceLayout* out) {
  if (out == nullptr) return false;
  switch (declaration) {
    case SourceDeclaration::kAndroid16:
      *out = LayoutOf<abi::arm64::android16::ThreadTlsPrefix>(); return true;
    case SourceDeclaration::kAndroid17:
      *out = LayoutOf<abi::arm64::android17::ThreadTlsPrefix>(); return true;
  }
  return false;
}
}  // namespace artdcevm::thread_refresh
