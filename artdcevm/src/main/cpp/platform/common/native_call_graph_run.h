#pragma once
#include "platform/common/native_call_graph_commit.h"
#include "platform/common/native_hierarchy_session.h"
#include "platform/common/native_source_compilation.h"

namespace artdcevm::native_call_graph_windows {class Factory;}
namespace artdcevm::native_call_graph_run {
// Borrowed operation dependencies, prepared in the same original Runnable
// callback as RunBatch. The actual hierarchy.context object and all services
// outlive End, including SourceOwner::Close. No Context copy grants JNI access.
struct Bindings {
 native_hierarchy_session::Bindings hierarchy;
 native_source_class_loading::Service*loading=nullptr;
 const native_source_stop::Service*stop=nullptr;
 const native_source_dex_registry::Service*dex_registry=nullptr;
 native_call_graph_execution::Execution*execution=nullptr;
 const native_source_compilation::Service*compilation=nullptr;
 size_t self_extent=0;
};
// The concrete native lifetime shared by all Provider lifecycle callbacks.
// No original source pointer survives End. Graph/plan preparation remains with
// the operation factory; this owner supplies no alternative proof callbacks.
class Owner final {
 public:
 explicit Owner(const Bindings&bindings):bindings_(bindings){}
 ~Owner();Owner(const Owner&)=delete;Owner&operator=(const Owner&)=delete;
 bool Begin(void*actual_driver,std::string*);
 // Preserve the operation factory's preparation callbacks and route the
 // complete source lifecycle through this owner. Both states outlive RunBatch.
 // closed_graph stages/inspects the union and, at Collected, binds/registers
 // the commit service. This owner alone starts Collected continuation.
 bool AttachProvider(const native_owned_removal::Provider&preparation,
                     native_owned_removal::Provider*out,std::string*);
 bool BindPrivateRootRelease(void*owner,bool(*release)(void*,const char**)noexcept)noexcept;
 bool BeforeVerification(void*,const transaction_root::Iterator*,std::string*);
 void AfterVerification(void*,const transaction_root::Iterator*,bool)noexcept;
 bool ContinueCollected(const char**refusal=nullptr)noexcept;
 std::unique_ptr<source_commit::Services>Finalize(const void*holder,const source_commit::Plan**)noexcept;
 void Commit(const source_commit::Plan&)noexcept;
 void Refund()noexcept;
 void End(void*actual_driver,bool original_success)noexcept;
 // Explicit pre-Run failure closure; no private source allocation has entered.
 // Also used by destruction if the operation factory failed before RunBatch.
 void CloseBeforeRun()noexcept;
 const native_source_class_loading::Run*Loading()const noexcept{return loading_.get();}
 native_source_stop::Session*Stop()const noexcept{return stop_.get();}
 native_hierarchy_session::Session*Hierarchy()const noexcept{return hierarchy_.get();}
 native_hierarchy_private_ledger::Ledger*Ledger()const noexcept{return ledger_.get();}
 native_call_graph_commit::SourceOwner*CommitOwner()const noexcept{return commit_.get();}
 const native_source_compilation::Service*Compilation()const noexcept{return bindings_.compilation;}
 const native_source_compilation::Receipt*CompilationReceipt()const noexcept{return &compilation_receipt_;}
 bool Running()const noexcept;
 const void*Driver()const noexcept{return Running()?driver_:nullptr;}
 // Scalar lifetime check, including a failed source awaiting original cleanup.
 bool SourceViewsLive()const noexcept{return phase_!=Phase::Empty&&phase_!=Phase::Closed;}
 private:
 friend class native_call_graph_windows::Factory;
 enum class Phase {Empty,Beginning,Running,Continued,Finalized,Committed,Refunded,Closed};
 bool FailBegin(std::string*,const char*)noexcept;
 void DestroyViews()noexcept;
 static bool ReleasePrivateRoots(void*,const char**)noexcept;
 Bindings bindings_;void*driver_=nullptr;Phase phase_=Phase::Empty;
 native_source_compilation::Receipt compilation_receipt_;
 bool hierarchy_active_=false,ledger_active_=false;
 native_owned_removal::Provider preparation_{};bool provider_attached_=false;
 // Receipt of this provider's single successful ordinary preflight. Set only
 // by the routed callback, before the actual original Run can enter Begin.
 bool preflight_attempted_=false,preflight_complete_=false;
 jvmtiEnv*preflight_jvmti_=nullptr;
 void*private_roots_owner_=nullptr;
 bool(*release_private_roots_)(void*,const char**)noexcept=nullptr;
 std::unique_ptr<native_source_stop::Session>stop_;
 std::unique_ptr<native_source_class_loading::Run>loading_;
 std::unique_ptr<native_call_graph_commit::SourceOwner>commit_;
 std::unique_ptr<native_hierarchy_private_ledger::Ledger>ledger_;
 std::unique_ptr<native_call_graph_execution::SourceRefund>refund_;
 std::unique_ptr<native_hierarchy_session::Session>hierarchy_;
};
}
