#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace artdcevm::android13 {
// android-13.0.0_r1 libcxx/include/string, ABI v1, little-endian arm64.
// A const long-string representation borrows a NUL-terminated buffer only for
// a synchronous ART call. It is never constructed or destroyed by libc++ and
// cannot be passed to an operation that mutates or retains the string.
struct BorrowedPlatformString {
  size_t capacity_tag;
  size_t length;
  const char* data;
  explicit BorrowedPlatformString(const char* text)
      : capacity_tag((std::strlen(text) + 2) | size_t{1}),
        length(std::strlen(text)), data(text) {}
};
static_assert(sizeof(BorrowedPlatformString) == 24);
static_assert(alignof(BorrowedPlatformString) == 8);
static_assert(offsetof(BorrowedPlatformString, length) == 8);
static_assert(offsetof(BorrowedPlatformString, data) == 16);
}
