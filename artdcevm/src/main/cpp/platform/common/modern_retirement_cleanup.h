#pragma once
#include "api/hook_engine.h"
#include "platform/common/retired_field_registry.h"
#include "platform/common/retired_method_registry.h"
#include "platform/common/exclusive_owned_roots.h"
#include <vector>
namespace artdcevm::native_interpretation_registry {class Registry;}
namespace artdcevm::native_source_stop {class Session;}
namespace artdcevm::retirement {
struct Membership {void* allocator=nullptr;size_t owned=0,foreign=0;bool matched=false;};
// Must run under a real native exclusive-owned stop. roots[0] is a live loader;
// all native members remain rooted by the caller's accompanying class globals.
bool CalibrateAllocatorMembership(HookEngine&,void* observed_class_linker,
    const exclusive_owned_roots::Context&,const std::vector<void*>& owned_members,
    const std::vector<void*>& foreign_members,Membership*,std::string* error);
class CleanupBinding final {
 public:
 // Ordinary symbol binding only. The allocator destructor and Contains are
 // fixed named libartbase operations; no ClassLoader record layout is needed.
 bool Bind(HookEngine&,std::string*);
 bool InstallStopped(HookEngine&,const native_source_stop::Session&,void*self,
     const Membership&,RetiredFieldRegistry&,RetiredMethodRegistry&,
     native_interpretation_registry::Registry*,std::string*)const;
 private:
 friend bool InstallRetirementCleanup(HookEngine&,const Membership&,RetiredFieldRegistry&,
     RetiredMethodRegistry&,std::string*,native_interpretation_registry::Registry*);
 bool Install(HookEngine&,const Membership&,RetiredFieldRegistry&,RetiredMethodRegistry&,
     native_interpretation_registry::Registry*,std::string*)const;
 void*target_=nullptr;
 bool(*contains_)(const void*,const void*)=nullptr;
};
struct Owners {
 const Membership&membership;
 RetiredFieldRegistry&fields;
 RetiredMethodRegistry&methods;
};
// Retired entries are pruned immediately before their allocation arena is freed.
// All supplied registries outlive the installed hook. No loader record or
// allocation is retained. Interpretation identities are pruned by the same
// sole original cleanup observer; this does not remove Instrumentation entries.
bool InstallRetirementCleanup(HookEngine&,const Membership&,RetiredFieldRegistry&,
    RetiredMethodRegistry&,std::string* error,native_interpretation_registry::Registry* interpreted=nullptr);
size_t CleanupCalls();size_t CleanupFields();size_t CleanupMethods();
size_t CleanupInterpretedMethods();
// Identity of the installed process-owned observer, not a cleanup count or a
// caller-provided capability. Persistent interpretation publication requires
// this exact Registry to be pruned before its method allocations disappear.
bool HasInterpretationCleanup(const native_interpretation_registry::Registry&)noexcept;
}
