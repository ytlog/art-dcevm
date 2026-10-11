#pragma once
#include "platform/common/native_hierarchy_source_cache.h"
#include "platform/common/native_source_dex_registry.h"
#include "platform/common/native_source_class_loading.h"
namespace artdcevm::native_source_stop {class Session;}
namespace artdcevm::native_hierarchy_private_ledger {
// Final authority is supplied by the actual continuous source stop and cleanup
// owner. It must prove exact Holder/Plan, GC reservation, reflective/native roots,
// deopt and interpreter invalidation. An empty callback is never a grant.
class CleanupOwner {
 public:
 virtual ~CleanupOwner()=default;
 virtual bool Verify(void* driver,const transaction_root::Holder*,const source_commit::Plan&)const noexcept=0;
 virtual bool IsStopped(void* driver,const transaction_root::Holder*)const noexcept=0;
 virtual bool IsFinalWindow(void* driver,const transaction_root::Holder*)const noexcept=0;
 virtual void BeginCommit(const source_commit::Plan&)const noexcept=0;
};
namespace detail {struct Hooks;}
class Ledger final:public native_hierarchy_source_cache::RequiredServices {
 public:
 Ledger(void* driver,const runnable_jni::Context&,source_handle_roots::Pool&,
        const native_hierarchy_source_cache::BatchAccess&,
        const modern_field_metadata::FieldMetadata&,
        const native_source_dex_registry::Service&,const CleanupOwner*);
 ~Ledger();Ledger(const Ledger&)=delete;Ledger&operator=(const Ledger&)=delete;
 bool AttachHolder(const transaction_root::Holder*,const transaction_root::Metadata&)noexcept;
 // Exact live native owner identity only, never a semantic/publication grant.
 bool OwnsDriver(const void*)const noexcept;
 // Construction failed before original Run entered: no captured source
 // record, Holder, graph, publication or root registration may exist.
 bool CloseBeforeRun()noexcept;
 bool CollectManagedRoots(const prepared_roots::Registrar&);
 bool CanRefresh(const prepared_roots::Resolver&)const noexcept;
 void Refresh(const prepared_roots::Resolver&)noexcept;
 // End a borrowed native snapshot before releasing its GC reservation. Root
 // registration remains owned; a later real stop must refresh it again.
 void InvalidateRoots()noexcept;
 bool EnterSemanticInspection(const source_handle_roots::StoppedReader&)const noexcept override;
 void LeaveSemanticInspection()const noexcept override;
 bool FinalRootWindow()const noexcept override;
 bool ReadStoppedRoot(const void*,void**)const noexcept override;
 bool Collected(const source_commit::Plan&)noexcept;
 // Ordinary preparation proves the complete actual source owner cohort. The
 // borrowed stop outlives all publication and refund readers in this Ledger.
 bool PreparePublication(const native_source_stop::Session&,size_t*,std::string*);
 bool MatchPublication(void* driver,const void* holder,void* linker,void* cache,
                       void* loader,size_t* index)const noexcept;
 // Ordinary exact source-owned DEX/cache query before any owned roots are
 // dropped. No published or publishing private entry can be discarded.
 bool CanDiscard(const native_hierarchy_source_cache::RecordView&,const char** refusal=nullptr)const noexcept;
 void Refunded()noexcept; // AFTER every captured private object was validated.
 bool ReleaseAfterRun(bool success)noexcept; // No source storage reads here.
 bool PreparePrivate(const native_hierarchy_source_cache::RecordView&,
                     const native_hierarchy_cache::Source&,std::string*)const override;
 bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&)const noexcept override;
 bool VerifyPrivate(const native_hierarchy_source_cache::RecordView&,const native_hierarchy_cache::Source&,
                    const runnable_jni::Context&,const source_handle_roots::Pool&)const noexcept override;
 bool VerifyReadonlyStaged(const native_hierarchy_graph::Graph&,const native_hierarchy_source_cache::RecordView&)const noexcept override{return false;}
 bool VerifyReadonlyPrepared(const native_hierarchy_graph::Graph&,const native_hierarchy_source_cache::RecordView&,const native_hierarchy_graph::Prepared&)const noexcept override{return false;}
 bool VerifyReadonly(const native_hierarchy_graph::Graph&,const native_hierarchy_source_cache::RecordView&,const source_commit::Plan&,size_t)const noexcept override{return false;}
 bool VerifyFrozen(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,const native_hierarchy_cache::Source&)const noexcept override;
 bool VerifyCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 private:
 friend struct detail::Hooks;
 struct Entry {native_hierarchy_source_cache::RecordView record;native_hierarchy_cache::Source source;void* loader_root;bool publishing=false,published=false;};
 bool Read(const void*,void**)const noexcept;
 bool ReadRegisteredRoot(const void*,void**)const noexcept;
 bool PublicationRootWindow()const noexcept;
 bool Live()const noexcept;bool Paused()const noexcept;
 bool Record(const native_hierarchy_source_cache::RecordView&)const noexcept;
 bool ExactPlan(const source_commit::Plan&)const noexcept;
 Entry* Publication(void* linker,void* cache,void* loader)noexcept;
 void Published(Entry*)noexcept;
 const Entry* Find(const native_hierarchy_cache::Source&)const noexcept;
 void*driver_;const runnable_jni::Context&context_;source_handle_roots::Pool&pool_;
 const native_hierarchy_source_cache::BatchAccess&batch_;const modern_field_metadata::FieldMetadata&fields_;
 const native_source_dex_registry::Service&registry_;const CleanupOwner*cleanup_;
 mutable const source_handle_roots::StoppedReader*inspection_reader_=nullptr;
 const native_source_stop::Session*publication_stop_=nullptr;
 const transaction_root::Holder*holder_=nullptr;transaction_root::Metadata metadata_{};
 mutable const native_hierarchy_graph::Graph*graph_=nullptr;mutable std::vector<Entry> entries_;
 struct RootEntry{const void* token;void* value;};
 std::vector<RootEntry> root_entries_;prepared_roots::Bindings managed_roots_;
 bool registration_started_=false,roots_registered_=false,roots_refreshed_=false;
 Ledger*previous_=nullptr;bool active_=false,collected_=false,refunded_=false,released_=false;
 mutable bool committed_=false;bool failed_=false;
};
// Observes actual RegisterExistingDexCache originals within a captured source
// transaction. Boot AppendToBootClassPath has a different platform STL ABI and
// is deliberately not granted by this observer. Call under actual SuspendAll.
bool Install(HookEngine&,void* independently_observed_linker,std::string*);
}
