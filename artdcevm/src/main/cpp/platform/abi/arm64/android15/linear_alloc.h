#pragma once
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cstddef>
#include <cstdint>
namespace artdcevm::abi::arm64::android15 {
// android-15.0.0_r1 runtime/base/mutex.h release futex declaration;
// contention statistics have zero-sized bases in the release source.
struct LinearMutexPrefix {void* vtable;const char* name;uint8_t level;bool respond;
 int32_t state,owner;uint32_t recursion;bool recursive,timeout;uint32_t monitor;};
static_assert(sizeof(LinearMutexPrefix)==40);
// linear_alloc.h puts ArenaAllocator immediately after the Mutex. Arena
// contents remain opaque: only the actual named ArenaAllocator::Contains reads it.
inline constexpr size_t kArenaOffset=sizeof(LinearMutexPrefix);
using GetAllocator=void* (*)(void*,android11::ReleaseObjectPointer);
using ArenaContains=bool (*)(const void*,const void*);
inline constexpr const char* kGetAllocator="_ZN3art11ClassLinker26GetAllocatorForClassLoaderENS_6ObjPtrINS_6mirror11ClassLoaderEEE";
inline constexpr const char* kArenaContains="_ZNK3art14ArenaAllocator8ContainsEPKv";
}
