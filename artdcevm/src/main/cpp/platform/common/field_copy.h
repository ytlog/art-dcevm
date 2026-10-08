#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::field_copy {
// Borrowed metadata remains valid under ART's exclusive mutator lock. These
// POD views contain no platform or NDK STL objects and allocate no storage.
struct CopyClassView {
  void* parent;
  void* loader;
  const char* descriptor;
  size_t field_count;
  bool is_object;
};
struct CopyFieldView {
  void* field;
  const char* name;
  const char* descriptor;
};
struct FieldCopyAccess {
  CopyClassView (*klass)(void*, bool);
  CopyFieldView (*field)(void*, bool, size_t);
  uint64_t (*read)(void*, void*, char);
  void (*write)(void*, void*, char, uint64_t);
  void (*transfer_monitor)(void*, void*);
  void (*barrier)(void*);
};
// ART has already allocated replacement objects and suspended mutation. This
// operation performs no allocation, creates no JNI IDs, and cannot fail.
void CopyAndClearEvolutionFields(const FieldCopyAccess&, bool is_static,
    void* target, void* target_class, void* source, void* source_class) noexcept;
}
