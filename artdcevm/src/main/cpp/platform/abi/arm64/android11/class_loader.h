#pragma once
#include <cstddef>
namespace artdcevm::class_loader_abi::arm64 {
// Fixed ClassLinker::ClassLoaderData declaration in Android 11–17 r1.
// jweak/ClassTable*/LinearAlloc* are opaque borrowed pointers.
struct ClassLoaderData {
  void* weak_root;
  void* class_table;
  void* allocator;
};
static_assert(sizeof(void*) == 8);
static_assert(sizeof(ClassLoaderData) == 24);
static_assert(offsetof(ClassLoaderData, allocator) == 16);
}  // namespace artdcevm::class_loader_abi::arm64
