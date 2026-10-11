#pragma once
#include "platform/common/native_call_graph_execution.h"
#include "platform/common/native_initialized_transfer.h"
#include "platform/common/native_structural_reflection.h"
#include "platform/common/native_typed_heap_walk.h"
#include "platform/common/object_replacement_map.h"
#include "platform/common/native_replacement_root_census.h"
#include "platform/common/native_weak_replacement.h"
#include "platform/common/native_exception_entry_ownership.h"
#include "platform/common/native_jni_exception_calibration.h"
#include "platform/common/native_hierarchy_private_ledger.h"
#include "platform/common/prepared_root_registry.h"
#include "platform/common/native_source_compilation.h"
#include <array>

namespace artdcevm::native_hierarchy_session {class Session;}
namespace artdcevm::native_call_graph_commit {
class SourceOwner;
// Fixed named original operations. Binding is symbol/receiver identity only;
// it does not grant a source window, rooted plan or graph execution capability.
class Binding final {
 public:
 bool Bind(HookEngine&,const runtime_services::Snapshot&,std::string*);
 private:
 friend class Service;
 using Replace=void(*)(void*,const void*);
 using SetStatus=void(*)(void*,uint8_t);
 using Retain=void(*)(void*,abi::arm64::android11::ReleaseObjectPointer);
 void*runtime_=nullptr;void**runtime_slot_=nullptr;void*thread_list_=nullptr;void*vm_=nullptr;
 Replace replace_=nullptr;SetStatus status_=nullptr;Retain retain_=nullptr;
};
struct Dependencies {
 const Binding*binding=nullptr;
 const modern_field_metadata::FieldMetadata*metadata=nullptr;
 const modern_field_storage::Service*fields=nullptr;
 const native_structural_reflection::Service*reflection=nullptr;
 native_structural_reflection::Plan*reflection_plan=nullptr;
 const native_initialized_transfer::Proof*initialization=nullptr;
 const native_typed_heap_walk::Service*heap=nullptr;
 const native_typed_heap_walk::RootProvenance*root_provenance=nullptr;
 const native_weak_replacement::Service*weak_replacement=nullptr;
 const source_handle_roots::Service*handle_roots=nullptr;
 const native_exception_upcall_boundary::Service*upcall=nullptr;
 const native_exception_entry_ownership::Proof*native_entries=nullptr;
 const native_jni_exception_calibration::Service*jni_returns=nullptr;
 const native_cache::Service*cache=nullptr;
 native_schema_guard_verifier::Verifier*schema=nullptr;
 native_call_graph_execution::Execution*execution=nullptr;
 native_interpretation_registry::Registry*interpretation=nullptr;
 const native_source_compilation::Service*compilation=nullptr;
 const native_source_compilation::Receipt*compilation_receipt=nullptr;
 const native_source_stop::Session*stop=nullptr;
 const native_source_class_loading::Run*loading=nullptr;
 const modern_field_metadata::ReadContext*read=nullptr;
 void*self=nullptr;const void*driver=nullptr;const void*holder=nullptr;
 std::vector<modern_methods::Service*>methods;
};
 // Allocated in the original Runnable Collected callback BEFORE starting the
 // source stop. Register operands before Begin/WaitingGC; refresh in its stop.
// All operands are independently registered and refreshed by the operation's
// root registry. The same original final source stop consumes this adapter.
// No JIT/AOT invalidation, synthetic Thread, or process deoptimization is used.
class Service final:public source_commit::Services,
                    public native_hierarchy_private_ledger::CleanupOwner {
 public:
 static std::unique_ptr<Service>Prepare(const runnable_jni::Context&,
     source_commit::Plan&,const Dependencies&,std::string*);
 bool CollectManagedRoots(const prepared_roots::Registrar&);
 bool CanRefresh(const prepared_roots::Resolver&)const noexcept;
 void Refresh(const prepared_roots::Resolver&)noexcept;
 bool Validate(const source_commit::Plan&)const override;
 // All rejectable operands before original DexCache publication. Requires
 // the collected graph receipt; it cannot authorize final mutation.
 bool ValidatePrepared(const source_commit::Plan&)const;
 bool Verify(void*,const transaction_root::Holder*,const source_commit::Plan&)const noexcept override;
 bool IsStopped(void*,const transaction_root::Holder*)const noexcept override;
 bool IsFinalWindow(void*,const transaction_root::Holder*)const noexcept override;
 void BeginCommit(const source_commit::Plan&)const noexcept override;
 uint32_t Status(void*)const override;
 void SetStatus(void*,uint32_t)const override;
 void TransferInitialization(const source_commit::Plan&,size_t)const override;
 void CopyFields(bool,void*,void*,void*,void*)const override;
 void*ObjectClass(void*)const override;
 void MarkClassAndMethodsObsolete(void*,void*)const override;
 void UpdateReflectiveReferences(const source_commit::Plan&)const override;
 void ReleaseExecutionEntries()const override;
 void ReplaceObjectReferences()const override;
 void RestoreDeclaringClasses(const source_commit::Plan&)const override;
 void*NewClassAfterReplacement(void*,size_t)const override;
 void RetainOldClass(void*,void*)const override;
 void ClearInterpreterCaches()const override;
 private:
 friend class SourceOwner;
 Service(source_commit::Plan&plan,const Dependencies&deps):plan_(&plan),deps_(deps){}
 bool PlanLayoutMatches(const source_commit::Plan&)const noexcept;
 bool Window(bool final=true)const noexcept;
 bool ValidateFor(const source_commit::Plan&,bool final,bool schema_guards=true)const;
 bool ObserveReplacementRoots(bool final)noexcept;
 bool ObserveNativeBoundaryPrerequisites(bool final)noexcept;
 bool ValidateNativeBoundaryPrerequisites(bool final)const noexcept;
 void RequireCommit()const noexcept;
 source_commit::Plan*plan_;Dependencies deps_;
 // Root bindings address vector elements. Freeze the storage layout before
 // registration; a later resize must refuse before any binding is written.
 std::array<const void*,5>plan_storage_{};
 std::array<size_t,5>plan_counts_{};
 std::vector<size_t>origin_indices_;
 bool abstract_instances_=false;
 prepared_roots::Bindings plan_roots_;
 std::unique_ptr<object_replacement::ObjectReplacementMap>map_;
 native_replacement_root_census::Report replacement_roots_{};
 mutable native_weak_replacement::Report weak_replacement_report_{};
 native_managed_stack_chain::Chain native_chain_;
 native_exception_upcall_boundary::Service::CaptureStorage upcall_storage_;
 const native_exception_upcall_boundary::Proof*upcall_proof_=nullptr;
 bool native_prerequisites_final_=false;
 bool replacement_roots_final_=false;
 mutable bool final_validated_=false,begun_=false,execution_released_=false,replaced_=false,restored_=false,cache_cleared_=false;
 bool registration_started_=false,registered_=false,refreshed_=false;
};
// The private DEX ledger is constructed before original Run, while Service
// needs the actual Collected plan. Keep this stable owner alive across both;
// bind exactly once after staging Service and before collecting ledger roots.
// No deferred pointer or callback supplies execution/GC permission by itself.
class SourceOwner final:public native_hierarchy_private_ledger::CleanupOwner,
                        public native_schema_guard_verifier::HierarchyAuthority {
 public:
 SourceOwner(const runnable_jni::Context&,const native_source_class_loading::Run&,
             const void*driver,native_source_stop::Session&,
             const native_source_compilation::Service&,const native_source_compilation::Receipt&)noexcept;
 SourceOwner(const SourceOwner&)=delete;SourceOwner&operator=(const SourceOwner&)=delete;
 // Own the staged adapter until finalize transfers it to the sole Run request.
 // The request retains it through end_run, while service_ is a borrowed view.
 bool Bind(std::unique_ptr<Service>,std::string*);
 Service*PreparedService()const noexcept{return staged_service_.get();}
 // Before roots are registered or a stop begins. The ledger and this owner
 // remain alive until the original Run has completed its registry calls.
 bool PreparePublication(const runnable_jni::Context&,
     native_hierarchy_private_ledger::Ledger&,void*class_linker,std::string*);
 bool RegisterRoots(const runnable_jni::Context&,prepared_roots::Registry&,
     native_hierarchy_graph::Authority&,native_hierarchy_private_ledger::Ledger&,
     std::string*);
 // Registry, authority and ledger objects outlive ReleaseAfterSuccess/Failure,
 // including their prior ReleaseAfterRun calls; destroy them after Close.
 // Actual stop only. Preflight every participant before writing any prepared
 // location; final refresh uses the same registry and never allocates.
 bool RefreshRoots(const char**refusal=nullptr)noexcept;
 // Refresh all participants in the real Collected stop, then seal graph
 // execution before hierarchy authority. A refusal is still
 // refundable here; the caller unwinds the stop before reporting failure.
 bool Collected(native_hierarchy_session::Session&,const char**refusal=nullptr)noexcept;
 // Collected callback's single actual stop/validation/continuation episode.
 // All allocation and root registration must already have completed.
 bool ContinueCollected(const runnable_jni::Context&,
     native_hierarchy_session::Session&,size_t self_extent,const char**refusal=nullptr)noexcept;
 // Provider::finalize refreshes every already registered participant here,
 // in the original final stop. Original publication has happened;
 // an invariant failure here must never return an ordinary refundable error.
 void Finalize(void*driver,const transaction_root::Holder*)noexcept;
 std::unique_ptr<source_commit::Services>TakeFinalService(const void*holder,
     const source_commit::Plan**plan)noexcept;
 void Commit(const source_commit::Plan&)noexcept;
 // end_run invokes this after successful original Run and hierarchy/ledger
 // release, before completing/destroying the loading Run. No Holder, native
 // method, Class or DEX memory is inspected after original cleanup.
 void ReleaseAfterSuccess(void*driver)noexcept;
 void ReleaseAfterFailure(void*driver,const native_hierarchy_session::Session&)noexcept;
 // After private record validation and owned entry refund, while original
 // source locals still exist. Invalidates borrowed receipts before clearing
 // the operation's prepared-root cohort; no native metadata is inspected.
 bool DropPreparedReferences(void*driver)noexcept;
 bool VerifyHierarchy(const source_commit::Plan&,size_t,void*loader)const noexcept override;
 bool Verify(void*,const transaction_root::Holder*,const source_commit::Plan&)const noexcept override;
 bool IsStopped(void*,const transaction_root::Holder*)const noexcept override;
 bool IsFinalWindow(void*,const transaction_root::Holder*)const noexcept override;
 void BeginCommit(const source_commit::Plan&)const noexcept override;
 private:
 bool SourceMatches(const void*,const void*)const noexcept;
 void InvalidateRoots()noexcept;
 void Close()noexcept;
 const native_source_class_loading::Run&loading_;
 native_source_stop::Session&stop_;
 const native_source_compilation::Service&compilation_;
 const native_source_compilation::Receipt&compilation_receipt_;
 const void*driver_=nullptr;const void*holder_=nullptr;void*self_=nullptr;
 std::unique_ptr<Service>staged_service_;
 Service*service_=nullptr;
 prepared_roots::Registry*root_registry_=nullptr;
 native_hierarchy_graph::Authority*authority_=nullptr;
 native_hierarchy_private_ledger::Ledger*ledger_=nullptr;
 void*publication_linker_=nullptr;
 bool references_dropped_=false;
};
}
