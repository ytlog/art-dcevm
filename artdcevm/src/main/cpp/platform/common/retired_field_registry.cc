#include "platform/common/retired_field_registry.h"

#include <cstdlib>

namespace artdcevm::retirement {

void RetiredFieldRegistry::Retire(void* field) {
  std::lock_guard<std::mutex> lock(state_->mutex_);
  if (state_->token_by_field_.find(field) != state_->token_by_field_.end()) return;
  uint32_t index;
  if (!state_->free_indices_.empty()) {
    index = state_->free_indices_.back();
    state_->free_indices_.pop_back();
    if (state_->field_by_index_[index] != nullptr) std::abort();
    state_->field_by_index_[index] = field;
  } else {
    if (state_->field_by_index_.size() >= kRetiredFieldTag) std::abort();
    index = static_cast<uint32_t>(state_->field_by_index_.size());
    state_->field_by_index_.push_back(field);
  }
  state_->token_by_field_.emplace(field, index | kRetiredFieldTag);
  if(state_->next_generation_==0)std::abort();
  state_->generation_by_field_.emplace(field,state_->next_generation_++);
}

uint32_t RetiredFieldRegistry::TokenFor(void* field) const {
  std::lock_guard<std::mutex> lock(state_->mutex_);
  auto it = state_->token_by_field_.find(field);
  return it == state_->token_by_field_.end() ? 0 : it->second;
}

uint64_t RetiredFieldRegistry::RetirementGenerationFor(void* field)const{return state_->IdentityFor(field).generation;}
RetiredFieldRegistry::RetirementIdentity RetiredFieldRegistry::Lifetime::IdentityFor(void* field)const {
 if(!Alive())return {0,0};
 std::lock_guard<std::mutex> lock(mutex_);if(!Alive())return {0,0};
 auto token=token_by_field_.find(field);auto generation=generation_by_field_.find(field);
 return {token==token_by_field_.end()?0:token->second,generation==generation_by_field_.end()?0:generation->second};
}

void* RetiredFieldRegistry::Resolve(uint32_t token) const {
  if ((token & kRetiredFieldTag) == 0) return nullptr;
  std::lock_guard<std::mutex> lock(state_->mutex_);
  uint32_t index = token & ~kRetiredFieldTag;
  return index < state_->field_by_index_.size() ? state_->field_by_index_[index] : nullptr;
}

size_t RetiredFieldRegistry::Count() const {
  std::lock_guard<std::mutex> lock(state_->mutex_);
  return state_->token_by_field_.size();
}

size_t RetiredFieldRegistry::SlotCount() const {
  std::lock_guard<std::mutex> lock(state_->mutex_);
  return state_->field_by_index_.size();
}

size_t RetiredFieldRegistry::Prune(const void* allocator, ContainsUnsafe contains_unsafe) {
  std::lock_guard<std::mutex> lock(state_->mutex_);
  size_t removed = 0;
  for (auto it = state_->token_by_field_.begin(); it != state_->token_by_field_.end();) {
    if (!contains_unsafe(allocator, it->first)) {
      ++it;
      continue;
    }
    uint32_t index = it->second & ~kRetiredFieldTag;
    if (index >= state_->field_by_index_.size() || state_->field_by_index_[index] != it->first)
      std::abort();
    state_->field_by_index_[index] = nullptr;
    state_->free_indices_.push_back(index);
    state_->generation_by_field_.erase(it->first);
    it = state_->token_by_field_.erase(it);
    ++removed;
  }
  return removed;
}

}  // namespace artdcevm::retirement
