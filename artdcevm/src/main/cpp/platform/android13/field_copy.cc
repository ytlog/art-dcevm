#include "platform/android13/field_copy.h"
#include <cstring>
namespace artdcevm::android13 {
namespace {
void* MatchingClass(const FieldCopyAccess& ops, void* target, const CopyClassView& old,
                    bool is_static) noexcept {
  for (auto* cursor = target; cursor != nullptr;) {
    const auto candidate = ops.klass(cursor, is_static);
    if (candidate.loader == old.loader && std::strcmp(candidate.descriptor, old.descriptor) == 0)
      return cursor;
    cursor = candidate.parent;
  }
  return nullptr;
}
CopyFieldView MatchingField(const FieldCopyAccess& ops, void* destination, bool is_static,
                            const CopyFieldView& old) noexcept {
  const auto view = ops.klass(destination, is_static);
  for (size_t i = 0; i < view.field_count; ++i) {
    auto candidate = ops.field(destination, is_static, i);
    if (std::strcmp(candidate.name, old.name) == 0 &&
        std::strcmp(candidate.descriptor, old.descriptor) == 0) return candidate;
  }
  return {};
}

}
void CopyAndClearEvolutionFields(const FieldCopyAccess& ops, bool is_static,
    void* target, void* target_class, void* source, void* source_class) noexcept {
  // Search from the new root for every old declaring class. Adding/removing a
  // parent changes depth, and a field with the same name in a child is unrelated.
  for (void* cursor = source_class; cursor != nullptr;) {
    const auto old = ops.klass(cursor, is_static);
    if (auto* destination = MatchingClass(ops, target_class, old, is_static)) {
      for (size_t i = 0; i < old.field_count; ++i) {
        auto field = ops.field(cursor, is_static, i);
        auto replacement = MatchingField(ops, destination, is_static, field);
        if (replacement.field) {
          auto value = ops.read(source, field.field, field.descriptor[0]);
          ops.write(target, replacement.field, field.descriptor[0], value);
        }
      }
    }
    if (is_static || !old.parent || ops.klass(old.parent, false).is_object) break;
    cursor = old.parent;
  }
  // Preserve the original sequence: complete every copy, move the monitor,
  // barrier the replacement, then clear all old fields and barrier the source.
  ops.transfer_monitor(target, source);
  ops.barrier(target);
  for (void* cursor = source_class; cursor != nullptr;) {
    const auto old = ops.klass(cursor, is_static);
    for (size_t i = 0; i < old.field_count; ++i) {
      auto field = ops.field(cursor, is_static, i);
      ops.write(source, field.field, field.descriptor[0], 0);
    }
    if (is_static || !old.parent || ops.klass(old.parent, false).is_object) break;
    cursor = old.parent;
  }
  ops.barrier(source);
}
}
