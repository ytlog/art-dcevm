#pragma once
#include "platform/abi/arm64/common/class_linker.h"

namespace artdcevm::abi::arm64::android13 {
// runtime/class_linker.h, android-13.0.0_r1. Release ObjPtr<Class>
// is a single pointer; this visitor is invoked without thread suspension.
using ClassVisitor = abi::arm64::ClassVisitor;
// libdexfile/dex/dex_file_types.h: dex::TypeIndex.
using TypeIndex = abi::arm64::TypeIndex;
}  // namespace artdcevm::abi::arm64::android13
