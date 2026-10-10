#pragma once
#include "platform/common/art_method_metadata.h"
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13::art_method_abi {
// Fixed API 33 alias; other versions require their own calibrated provider.
using ArtMethod = method_metadata::Method12;
using method_metadata::ReadAccessFlags;
inline constexpr size_t kArrayDataOffset = 8;
static_assert(sizeof(ArtMethod) == 32 && alignof(ArtMethod) == 8);
static_assert(offsetof(ArtMethod, access_flags) == 4);
static_assert(offsetof(ArtMethod, dex_method_index) == 8);
}
