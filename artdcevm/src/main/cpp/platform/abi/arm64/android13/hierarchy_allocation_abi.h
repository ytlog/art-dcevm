#pragma once
#include "platform/abi/arm64/android13/allocation_validation_abi.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/abi/arm64/android11/gc_cache_pair.h"
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13::hierarchy_allocation_abi {
// Locked android-13.0.0_r1 DexCache and platform libc++ vector. Only borrowed
// storage is inspected; no platform STL object is constructed or destroyed.
struct Vector { const unsigned char* begin; const unsigned char* end; const unsigned char* capacity; };
#pragma pack(push,4)
struct Cache {
  validation_abi::DexCachePrefix prefix;
  uint64_t preresolved_strings, call_sites, fields, method_types, methods, types, strings;
  uint32_t num_preresolved_strings, num_call_sites, num_fields, num_method_types;
  uint32_t num_methods, num_types, num_strings;
};
#pragma pack(pop)
using TypePair=abi::arm64::android11::GcCachePair;
struct alignas(16) NativePair { uintptr_t object, index; };
struct Target { void* owner; void* klass; void* cache; bool structural; };
static_assert(sizeof(Vector)==24 && sizeof(Cache)==108);
static_assert(offsetof(Cache,types)==64 && offsetof(Cache,num_types)==100);
static_assert(sizeof(TypePair)==8 && sizeof(NativePair)==16);
inline constexpr int32_t kCacheSlot=3,kClassSlot=4;
}
