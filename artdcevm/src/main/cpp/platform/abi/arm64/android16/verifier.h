#pragma once
#include "platform/abi/arm64/common/runtime_objects.h"

// android-16.0.0_r1 runtime/verifier/reg_type_cache.{h,cc}: precision was
// removed and FromClass uses the cache's own loader and DEX. ART 17 agrees.
namespace artdcevm::abi::arm64::android16::verifier {
using FromClass = const void* (*)(void*, void*);
using GetComponentType = const void* (*)(void*, const void*);
using AssignableFrom = bool (*)(const void*, const void*, const void*, bool);
using From = const void* (*)(void*, const char*);
using Construct = void (*)(void*, void*, void*, void*, Handle, const void*, bool, bool);
}
