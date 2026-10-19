#pragma once
#include <cstddef>
#include <string_view>
#include "platform/abi/arm64/common/runtime_objects.h"

// base/pointer_size.h and mirror/class.h, fixed Android 11--14 (including 12L): PointerSize : size_t.
namespace artdcevm::abi::arm64::android11 {
enum class PointerSize : size_t { k64 = 8 };
using FindInterfaceMethod = ArtMethodPrefix* (*)(void*, std::string_view, std::string_view, PointerSize);
}  // namespace artdcevm::abi::arm64::android11
