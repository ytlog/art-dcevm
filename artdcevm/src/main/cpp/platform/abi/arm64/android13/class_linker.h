#pragma once
#include <cstdint>

namespace artdcevm::abi::arm64::android13 {
// runtime/class_linker.h, android-13.0.0_r1. Release ObjPtr<Class>
// is a single pointer; this visitor is invoked without thread suspension.
struct ClassVisitor {
  virtual ~ClassVisitor() = default;
  virtual bool operator()(void* klass) = 0;
};
// libdexfile/dex/dex_file_types.h: dex::TypeIndex.
struct TypeIndex { uint16_t index; };
static_assert(sizeof(TypeIndex) == 2);
}  // namespace artdcevm::abi::arm64::android13
