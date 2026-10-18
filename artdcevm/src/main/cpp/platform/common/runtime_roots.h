#pragma once
#include <array>
#include <cstdint>
#include "platform/abi/arm64/common/handle_scope_visitor.h"
#include "platform/abi/arm64/interface_runtime_table.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/abi/arm64/common/runtime_thread.h"

namespace artdcevm::runtime_roots {
using namespace abi::arm64;
template <class T> T& Field(void* object, size_t offset) {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
// Observe ART's existing scope chain. No candidate root is installed, no object
// is dereferenced, and the callback only records addresses into fixed storage.
class ScopeRoots final : public RootVisitor {
 public:
  static constexpr size_t kMaxRoots = 64;
  static constexpr size_t kMaxHeader = abi::arm64::kMaxInterfaceScopeHeader;
  explicit ScopeRoots(void* scope, size_t capacity)
      : begin(reinterpret_cast<uintptr_t>(scope)), end(begin + kMaxHeader + 4 * capacity) {}
  void VisitRoots(void***, size_t, const RootInfo&) noexcept override { unexpected = true; }
  void VisitRoots(uint32_t** roots, size_t count, const RootInfo&) noexcept override {
    for (size_t i = 0; i < count; ++i) {
      const uintptr_t address = reinterpret_cast<uintptr_t>(roots[i]);
      if (address >= begin && address < end && (address - begin) % 4 == 0)
        observed[(address - begin) / 4] = true;
    }
  }
  bool Matches(const InterfaceRuntime& layout, void* scope, const uint32_t* target,
               uint32_t size) const {
    const auto* start = static_cast<uint8_t*>(scope) + layout.scope_header_size;
    const uintptr_t address = reinterpret_cast<uintptr_t>(target);
    if (unexpected || address < reinterpret_cast<uintptr_t>(start) ||
        address >= reinterpret_cast<uintptr_t>(start + 4 * size)) return false;
    for (size_t offset = 0; begin + offset < end; offset += 4) {
      const bool expected = offset >= layout.scope_header_size &&
          offset < layout.scope_header_size + 4 * size &&
          Field<uint32_t>(scope, offset) != 0;
      if (observed[offset / 4] != expected) return false;
    }
    return observed[(address - begin) / 4];
  }
 private:
  uintptr_t begin, end;
  std::array<bool, kMaxRoots + kMaxHeader / 4> observed{};
  bool unexpected = false;
};
inline bool VerifyScopes(const InterfaceRuntime& layout, const RuntimeThread& thread_layout,
                         void* thread, Handle target, const HandleScopeVisitors& visitors) {
  void* scope = Field<void*>(thread, thread_layout.top_handle_scope);
  // Both fixed sources create a StackHandleScope<1> immediately around the
  // admission call. Check its known root address BEFORE reading the candidate
  // header: a mismatched Thread slot must never be followed as a pointer.
  if (!scope || reinterpret_cast<uintptr_t>(scope) + layout.scope_header_size !=
                    reinterpret_cast<uintptr_t>(target.reference)) return false;
  const int32_t capacity = Field<int32_t>(scope, layout.scope_capacity);
  const uint32_t size = layout.scope_size == InterfaceRuntime::kAbsent
      ? capacity : Field<uint32_t>(scope, layout.scope_size);
  if (capacity != 1 || size != 1) return false;
  ScopeRoots observed(scope, capacity);
  if (!visitors.Visit(layout.scope_thread_id_signed, thread, &observed,
                               Field<uint32_t>(thread, thread_layout.tid))) return false;
  return observed.Matches(layout, scope, target.reference, size);
}

// A single StackHandleScope root, using only declarations accepted by VerifyScopes.
class Root {
 public:
  Root(void* thread, const RuntimeThread& thread_layout, const InterfaceRuntime& layout,
       uintptr_t* object)
      : thread_(thread), thread_layout_(thread_layout), layout_(layout), object_(object) {
    Field<void*>(storage_.data(), layout_.scope_link) = Field<void*>(thread_, thread_layout_.top_handle_scope);
    Field<int32_t>(storage_.data(), layout_.scope_capacity) = 1;
    *Slot() = static_cast<uint32_t>(*object_);
    if (layout_.scope_size != InterfaceRuntime::kAbsent)
      Field<uint32_t>(storage_.data(), layout_.scope_size) = 1;
    Field<void*>(thread_, thread_layout_.top_handle_scope) = storage_.data();
  }
  ~Root() {
    *object_ = *Slot();
    Field<void*>(thread_, thread_layout_.top_handle_scope) = Field<void*>(storage_.data(), layout_.scope_link);
  }
  Root(const Root&) = delete;
  Root& operator=(const Root&) = delete;
 private:
  uint32_t* Slot() { return reinterpret_cast<uint32_t*>(storage_.data() + layout_.scope_header_size); }
  void* thread_;
  const RuntimeThread& thread_layout_;
  const InterfaceRuntime& layout_;
  uintptr_t* object_;
  alignas(void*) std::array<uint8_t, kMaxInterfaceScopeHeader + 4> storage_{};
};
}  // namespace artdcevm::runtime_roots
