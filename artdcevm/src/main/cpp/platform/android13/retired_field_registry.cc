#include "platform/android13/retired_field_registry.h"

#include <cstdlib>

namespace artdcevm::android13 {

void RetiredFieldRegistry::Retire(void* field) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (token_by_field_.find(field) != token_by_field_.end()) return;
  uint32_t index;
  if (!free_indices_.empty()) {
    index = free_indices_.back();
    free_indices_.pop_back();
    if (field_by_index_[index] != nullptr) std::abort();
    field_by_index_[index] = field;
  } else {
    if (field_by_index_.size() >= kRetiredFieldTag) std::abort();
    index = static_cast<uint32_t>(field_by_index_.size());
    field_by_index_.push_back(field);
  }
  token_by_field_.emplace(field, index | kRetiredFieldTag);
}

uint32_t RetiredFieldRegistry::TokenFor(void* field) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = token_by_field_.find(field);
  return it == token_by_field_.end() ? 0 : it->second;
}

void* RetiredFieldRegistry::Resolve(uint32_t token) const {
  if ((token & kRetiredFieldTag) == 0) return nullptr;
  std::lock_guard<std::mutex> lock(mutex_);
  uint32_t index = token & ~kRetiredFieldTag;
  return index < field_by_index_.size() ? field_by_index_[index] : nullptr;
}

size_t RetiredFieldRegistry::Count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return token_by_field_.size();
}

size_t RetiredFieldRegistry::SlotCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return field_by_index_.size();
}

size_t RetiredFieldRegistry::Prune(const void* allocator, ContainsUnsafe contains_unsafe) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t removed = 0;
  for (auto it = token_by_field_.begin(); it != token_by_field_.end();) {
    if (!contains_unsafe(allocator, it->first)) {
      ++it;
      continue;
    }
    uint32_t index = it->second & ~kRetiredFieldTag;
    if (index >= field_by_index_.size() || field_by_index_[index] != it->first)
      std::abort();
    field_by_index_[index] = nullptr;
    free_indices_.push_back(index);
    it = token_by_field_.erase(it);
    ++removed;
  }
  return removed;
}

}  // namespace artdcevm::android13
