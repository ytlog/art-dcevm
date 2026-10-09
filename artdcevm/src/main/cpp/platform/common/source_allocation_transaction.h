#pragma once
#include "platform/common/transaction_root_view.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/rooted_hierarchy_allocator.h"
namespace artdcevm::source_allocation_transaction {
// Allocation starts before initialized/newClasses/newInstances exist. The real
// source Holder alone owns mirror/cache roots; no final-phase synthetic Plan is
// accepted here. Returned slots belong to the surrounding Runnable root Pool.
struct Snapshot {
 transaction_root::Metadata metadata;
 std::vector<rooted_hierarchy::Target> targets;
};
bool Read(HookEngine&,const modern_field_metadata::FieldMetadata&,
 const transaction_root::Holder* actual_holder,source_handle_roots::Pool&,
 Snapshot*,std::string*);
}
