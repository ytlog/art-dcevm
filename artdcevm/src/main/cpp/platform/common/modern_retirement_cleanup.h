#pragma once
#include "api/hook_engine.h"
#include "platform/common/retired_field_registry.h"
#include "platform/common/retired_method_registry.h"
#include "platform/common/exclusive_owned_roots.h"
#include <vector>
namespace artdcevm::retirement {
struct Membership {void* allocator=nullptr;size_t owned=0,foreign=0;bool matched=false;};
// Must run under a real native exclusive-owned stop. roots[0] is a live loader;
// all native members remain rooted by the caller's accompanying class globals.
bool CalibrateAllocatorMembership(HookEngine&,void* observed_class_linker,
    const exclusive_owned_roots::Context&,const std::vector<void*>& owned_members,
    const std::vector<void*>& foreign_members,Membership*,std::string* error);
// Retired entries are pruned before ART prepares/destroys their loader record.
// Both registries are durable owners. No loader record or allocation is retained.
bool InstallRetirementCleanup(HookEngine&,const Membership&,RetiredFieldRegistry&,
    RetiredMethodRegistry&,std::string* error);
size_t CleanupCalls();size_t CleanupFields();size_t CleanupMethods();
}
