#pragma once

#include <cstddef>
#include <mutex>
#include <unordered_set>

namespace artdcevm::android13 {

// ART owns the methods and allocators. The registry only tracks opaque native
// identities until the corresponding LinearAlloc is about to be destroyed.
class RetiredMethodRegistry {
 public:
  using ContainsUnsafe = bool (*)(const void* allocator, void* method);

  void Retire(void* method);
  bool Contains(void* method) const;
  size_t Count() const;
  size_t Prune(const void* allocator, ContainsUnsafe contains_unsafe);

 private:
  mutable std::mutex mutex_;
  std::unordered_set<void*> retired_;
};

}  // namespace artdcevm::android13
