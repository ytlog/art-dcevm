#pragma once

#include "platform/android13/retired_field_registry_abi.h"
#include <mutex>
#include <unordered_map>
#include <vector>

namespace artdcevm::android13 {

class RetiredFieldRegistry {
 public:
  using ContainsUnsafe = bool (*)(const void* allocator, void* field);

  void Retire(void* field);
  uint32_t TokenFor(void* field) const;
  void* Resolve(uint32_t token) const;
  size_t Count() const;
  size_t SlotCount() const;
  size_t Prune(const void* allocator, ContainsUnsafe contains_unsafe);

 private:
  mutable std::mutex mutex_;
  std::unordered_map<void*, uint32_t> token_by_field_;
  std::vector<void*> field_by_index_;
  std::vector<uint32_t> free_indices_;
};

}  // namespace artdcevm::android13
