#include "platform/android13/retired_method_registry.h"

namespace artdcevm::android13 {

void RetiredMethodRegistry::Retire(void* method) {
  std::lock_guard<std::mutex> lock(mutex_);
  retired_.insert(method);
}

bool RetiredMethodRegistry::Contains(void* method) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return retired_.find(method) != retired_.end();
}

size_t RetiredMethodRegistry::Count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return retired_.size();
}

size_t RetiredMethodRegistry::Prune(const void* allocator, ContainsUnsafe contains_unsafe) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t removed = 0;
  for (auto it = retired_.begin(); it != retired_.end();) {
    if (!contains_unsafe(allocator, *it)) {
      ++it;
      continue;
    }
    it = retired_.erase(it);
    ++removed;
  }
  return removed;
}

}  // namespace artdcevm::android13
