#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "platform/abi/arm64/common/reflective_value_visitor.h"

namespace artdcevm::member_mapping {
// JniIdManager owns the ID map and its visit/locking. Its fixed 11--17 repair
// assumes each changed method has a slot in its declaring Class's array,
// although obsolete (and since 14 conflict) IDs bypass arrays at encoding.
// During that single repair, hide only an unowned declaration's ClassExt.
// No managed allocation/GC/suspension occurs; restore the identical reference
// before the next visitor call and before returning from the manager.
class JniIdArrayVisitor final : public abi::arm64::ReflectiveValueVisitor {
 public:
  using UnownedExt = uint32_t* (*)(abi::arm64::ArtMethod*) noexcept;
  JniIdArrayVisitor(abi::arm64::ReflectiveValueVisitor* original, UnownedExt unowned)
      : original_(original), unowned_(unowned) {}
  ~JniIdArrayVisitor() override { Restore(); }
  abi::arm64::ArtMethod* VisitMethod(abi::arm64::ArtMethod* method,
                                    const abi::arm64::ReflectionSourceInfo& info) noexcept override {
    Restore();
    auto* replacement = original_->VisitMethod(method, info);
    if (method != replacement) { Mask(unowned_(method)); Mask(unowned_(replacement)); }
    return replacement;
  }
  abi::arm64::ArtField* VisitField(abi::arm64::ArtField* field,
                                  const abi::arm64::ReflectionSourceInfo& info) noexcept override {
    Restore();
    return original_->VisitField(field, info);
  }
 private:
  struct Saved { uint32_t* slot = nullptr; uint32_t value = 0; };
  void Mask(uint32_t* slot) noexcept {
    if (!slot) return;
    for (size_t i = 0; i < count_; ++i) if (saved_[i].slot == slot) return;
    saved_[count_++] = {slot, *slot};
    *slot = 0;
  }
  void Restore() noexcept {
    while (count_) { const auto& saved = saved_[--count_]; *saved.slot = saved.value; }
  }
  abi::arm64::ReflectiveValueVisitor* original_;
  UnownedExt unowned_;
  std::array<Saved, 2> saved_{};
  size_t count_ = 0;
};
}  // namespace artdcevm::member_mapping
