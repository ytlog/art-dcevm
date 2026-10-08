#pragma once
#include "platform/common/retired_field_token.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
namespace artdcevm::retirement {
class RetiredFieldRegistry {
 public:
  using ContainsUnsafe=bool (*)(const void*,void*);
  struct RetirementIdentity {uint32_t token;uint64_t generation;};
  // Aliases retain this native identity state, never the registry wrapper. The
  // lifetime cookie distinguishes a different owner at the same C++ address.
  class Lifetime {
   public:
    RetirementIdentity IdentityFor(void*)const;
    bool Alive()const{return alive_.load(std::memory_order_acquire);}
   private:
    friend class RetiredFieldRegistry;
    std::atomic<bool> alive_{true};mutable std::mutex mutex_;
    std::unordered_map<void*,uint32_t> token_by_field_;
    std::vector<void*> field_by_index_;std::vector<uint32_t> free_indices_;
    std::unordered_map<void*,uint64_t> generation_by_field_;uint64_t next_generation_=1;
  };
  RetiredFieldRegistry():state_(std::make_shared<Lifetime>()){}
  ~RetiredFieldRegistry(){state_->alive_.store(false,std::memory_order_release);}
  RetiredFieldRegistry(const RetiredFieldRegistry&)=delete;
  RetiredFieldRegistry& operator=(const RetiredFieldRegistry&)=delete;
  RetiredFieldRegistry(RetiredFieldRegistry&&)=delete;
  RetiredFieldRegistry& operator=(RetiredFieldRegistry&&)=delete;
  std::shared_ptr<const Lifetime> LifetimeState()const{return state_;}
  void Retire(void*);
  uint32_t TokenFor(void*)const;
  uint64_t RetirementGenerationFor(void*)const;
  RetirementIdentity IdentityFor(void* field)const{return state_->IdentityFor(field);}
  void* Resolve(uint32_t)const;
  size_t Count()const;size_t SlotCount()const;
  size_t Prune(const void*,ContainsUnsafe);
 private:
  std::shared_ptr<Lifetime> state_;
};
}
