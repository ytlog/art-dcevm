#pragma once
#include <vector>
#include "platform/common/source_commit.h"
#include "platform/common/modern_field_storage.h"
namespace artdcevm::native_initialized_transfer {
// Per-pair proof of completed or stable pre-initialization migration. It cannot
// prove an active ClassLinker InitializeClass episode, tid or waiter completion.
// Plan roots and the original exclusive/GC-disabled owner remain mandatory.
class Proof {
 public:
  enum class Action {Invalid,PreservePreInitialization,CompleteInitialization};
  struct Decision {Action action=Action::Invalid;uint32_t status=0;};
  bool Prepare(const source_commit::Plan&,const modern_field_metadata::FieldMetadata&,
      const modern_field_storage::Service&,const modern_field_metadata::ReadContext*);
  bool Validate(const source_commit::Plan&,const modern_field_metadata::ReadContext*)const noexcept;
  Decision Transfer(const source_commit::Plan&,size_t,
      const modern_field_metadata::ReadContext*)const noexcept;
  uint32_t DestinationStatus(const source_commit::Plan&,size_t,
      const modern_field_metadata::ReadContext*)const noexcept;
 private:
  struct PairState {Action action;uint32_t old_status,new_status;};
  std::vector<PairState> actions_;
  void* const* old_slots_=nullptr;
  void* const* new_slots_=nullptr;
  const source_commit::Plan* plan_=nullptr;
  const modern_field_metadata::ReadContext* context_=nullptr;
  void* mapping_owner_=nullptr;
  const modern_field_metadata::FieldMetadata* metadata_=nullptr;
  const modern_field_storage::Service* fields_=nullptr;
};
}
