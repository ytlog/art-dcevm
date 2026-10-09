#pragma once
#include "api/hook_engine.h"
#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/source_handle_roots.h"
namespace artdcevm::native_hierarchy_source_checks {
// The root's sole original Run owner opens this scope. It grants only the two
// original hierarchy comparisons or final/abstract flag mismatch, using actual
// incoming owner DEX and independently staged public graph. Final schema guards
// must still prove the allocated Plan; this scope grants no Run/publication access.
class Scope final {
 public:
 using Refuse=void(*)(void* actual_class_owner,int,const char*) noexcept;
 Scope(void* actual_driver,const runnable_jni::Context&,
       source_handle_roots::Service&,native_hierarchy_graph::Authority&,Refuse);
 ~Scope();Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
 bool Check(void* actual_class_owner,bool(*original)(void*));
 bool OwnsClassOwner(const void*)const noexcept;
 private:
 bool Proof(void*,jvmtiError* expected_failure=nullptr);
 bool Mark(void*);
 void* driver_;runnable_jni::Context context_;
 source_handle_roots::Service& roots_;native_hierarchy_graph::Authority& authority_;
 Refuse refuse_;Scope* previous_;
};
// Install only named fixed source declarations under actual ScopedSuspendAll.
// A successful installation is a hook capability, never graph/allocator readiness.
using FailureObserver=void(*)(void* actual_driver,jvmtiError)noexcept;
// Sole Run owner binds its actual-source failure observer here. Notification
// occurs only for forwarded original failures; the exact deferred hierarchy66/classflags71 is absent.
void SetFailureObserver(FailureObserver)noexcept;
bool Ready(FailureObserver exact_observer)noexcept;
// Typed failure bridge for allocator/session refusals. Uses the actual Class
// owner signature and sole original RecordFailure; never writes result fields.
void RefuseActualClass(void* actual_class_owner,int,const char*) noexcept;
bool Install(HookEngine&,std::string*);
}
