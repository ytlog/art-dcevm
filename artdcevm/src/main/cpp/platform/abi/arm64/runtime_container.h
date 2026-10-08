#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::runtime {
// Platform libc++ string/vector/list headers have the audited ARM64 release
// representation. Storage is borrowed only: never access it as NDK STL.
struct alignas(8) PlatformContainer { unsigned char storage[24]; };
static_assert(sizeof(PlatformContainer)==24 && alignof(PlatformContainer)==8);
}
