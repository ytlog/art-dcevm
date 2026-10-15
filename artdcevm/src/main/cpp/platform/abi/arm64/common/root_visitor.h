#pragma once
#include <cstddef>
#include <cstdint>

namespace artdcevm::abi::arm64 {
// runtime/gc_root.h, android-13.0.0_r1 and android-16.0.0_r1. RootInfo is
// passed by reference; neither its fields nor any root object are accessed.
struct RootInfo;
struct RootVisitor {
  virtual ~RootVisitor() = default;
  virtual void VisitRoots(void*** roots, size_t count, const RootInfo& info) = 0;
  virtual void VisitRoots(uint32_t** roots, size_t count, const RootInfo& info) = 0;
};
}  // namespace artdcevm::abi::arm64
