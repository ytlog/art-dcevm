#pragma once
#include <cstdint>
#include <string_view>
#include "platform/abi/arm64/common/runtime_objects.h"

// base/pointer_size.h and mirror/class.h, fixed Android 15--17: PointerSize : uint32_t.
namespace artdcevm::abi::arm64::android15 {
enum class PointerSize : uint32_t { k64 = 8 };
using FindClassMethod = ArtMethodPrefix* (*)(void*, std::string_view, std::string_view, PointerSize);
using FindInterfaceMethod = ArtMethodPrefix* (*)(void*, std::string_view, std::string_view, PointerSize);
}  // namespace artdcevm::abi::arm64::android15
