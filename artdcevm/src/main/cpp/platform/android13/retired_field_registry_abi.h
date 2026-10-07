#pragma once

#include <cstddef>
#include <cstdint>

namespace artdcevm::android13 {

inline constexpr uint32_t kRetiredFieldTag = uint32_t{1} << 31;

// Plain C-compatible callbacks across the NDK/platform C++ ABI boundary.
struct RetiredFieldCallbacks {
  void (*retire)(void*);
  uint32_t (*token_for)(void*);
};

}  // namespace artdcevm::android13
