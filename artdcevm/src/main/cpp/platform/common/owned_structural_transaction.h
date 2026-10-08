#pragma once
#include "platform/common/source_commit.h"
#include "platform/common/transaction_root_view.h"
#include "platform/common/modern_field_metadata.h"
namespace artdcevm::owned_structural_transaction {
// Native Holder/Iter are supplied by ART's actual structural-commit callback,
// under its exclusive mutator and disabled-GC scope. Their handles retain every
// borrowed managed root until the original transaction leaves that boundary.
struct StructuralTransaction {
  transaction_root::Metadata metadata;
  transaction_root::Roots roots;
  source_commit::Plan plan;
};
bool ReadStructuralTransaction(HookEngine&,
    const modern_field_metadata::FieldMetadata&,const void* real_iterator,
    StructuralTransaction*,std::string* error);
}
