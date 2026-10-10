#pragma once
#include <cstdint>
namespace artdcevm::android13::mutex_abi {
// Fixed runtime/base/mutex.h, Android 13 release ARM64 futex declaration.
struct Mutex {void* vtable;const char* name;uint8_t level;bool respond;
 int32_t state,owner;uint32_t recursion;bool recursive,timeout;uint32_t monitor;};
static_assert(sizeof(Mutex)==40);
}
