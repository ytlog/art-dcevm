#pragma once
#include "platform/common/source_commit.h"
#include "platform/common/modern_field_storage.h"
namespace artdcevm::native_initialized_transfer {
// Narrow proof of already completed initialization. It intentionally cannot
// prove an active ClassLinker InitializeClass episode, tid or waiter completion.
// Plan roots and the original exclusive/GC-disabled owner remain mandatory.
class Proof {
 public:
  bool Prepare(const source_commit::Plan&,const modern_field_metadata::FieldMetadata&,
      const modern_field_storage::Service&,const modern_field_metadata::ReadContext*);
  bool Validate(const source_commit::Plan&,const modern_field_metadata::ReadContext*)const noexcept;
  uint32_t DestinationStatus(const source_commit::Plan&,size_t,
      const modern_field_metadata::ReadContext*)const noexcept;
 private:
  const source_commit::Plan* plan_=nullptr;
  const modern_field_metadata::ReadContext* context_=nullptr;
  void* mapping_owner_=nullptr;
  const modern_field_metadata::FieldMetadata* metadata_=nullptr;
  const modern_field_storage::Service* fields_=nullptr;
};
}
