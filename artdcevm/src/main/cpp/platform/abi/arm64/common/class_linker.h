#pragma once
#include <cstdint>
namespace artdcevm::abi::arm64 {
// Identical release declarations in checked Android 13 and 16 sources:
// runtime/class_linker.h and libdexfile/dex/dex_file_types.h.
struct ClassVisitor { virtual ~ClassVisitor() = default; virtual bool operator()(void* klass) = 0; };
struct TypeIndex { uint16_t index; };
static_assert(sizeof(TypeIndex) == 2);
}  // namespace artdcevm::abi::arm64
