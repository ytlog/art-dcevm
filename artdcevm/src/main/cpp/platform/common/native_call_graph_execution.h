#pragma once
#include "platform/common/native_call_graph_inventory.h"
#include "platform/common/native_call_graph_stack.h"
#include "platform/common/native_call_graph_replacement.h"
#include "platform/common/native_interpretation_registry.h"
#include "platform/common/native_exception_upcall_boundary.h"
namespace artdcevm::native_hierarchy_session {struct Bindings;}

namespace artdcevm::native_call_graph_execution {
// One synchronous operation over the installed BEFORE graph and the actual
// replacement union. Complete union selection includes newly discovered old
// ancestors and all incoming bodies; incomplete resolution stays explicit.
// FinalDrained requires the independent original collected/frozen census and
// current complete thread walks. It grants no schema, GC or publication rights.
enum class Status {
 Empty, Prepared, PreparationRefused, UnsupportedIncompleteGraph,
 UnsupportedEmptySelection, InspectionRefused, UnsupportedActiveCompiled,
 InspectedDrained, ReplacementPrepared, ReplacementInspectedDrained,
 CollectedDrained, FinalRefused, FinalDrained
};
enum class GraphScope {InstalledBeforeOnly,InstalledBeforeAndReplacementBodies,InstalledUnionSelection};
struct Report {
 GraphScope scope=GraphScope::InstalledBeforeOnly;
 Status status=Status::Empty;
 native_selective_interpretation::Report activation{};
 native_call_graph_stack::Report inspected{},final{};
 native_selective_interpretation::Report replacement_activation{},union_activation{};
 native_call_graph_stack::Report replacement_inspected{},union_inspected{},replacement_final{},union_final{};
 native_call_graph_stack::Report before_reinspected{};
 size_t replacement_classes=0;
 size_t replacement_calls=0,replacement_call_gaps=0,replacement_native_boundaries=0;
 size_t union_nodes=0,union_calls=0,union_gaps=0;
 bool union_complete=false;
 bool replacement_bodies_drained=false;
 size_t graph_native_boundaries=0;
 bool compiled_drained=false,native_boundary_obligations=false;
 const char*failure=nullptr;
};
struct RefundReport {
 native_call_graph_stack::Report inspected{};
 native_selective_interpretation::RefundReport entries{},union_entries{},before_entries{};
 // All queried replacement Instrumentation entries are absent, including
 // foreign entries. Independent Class/cache/heap reference proof still applies.
 bool replacement_entries_released=false;
 bool owned_entries_removed=false;
 const char*failure=nullptr;
};
// Rooted union facts for the live old/new carriers. Loader tokens from the
// two producers are rebased by current rooted identity; native methods are
// never merged by member name. A union can retain explicit dispatch gaps.
// Completeness here remains a graph fact, independent of final source census,
// instrumentation, GC references, cache disposal and publication capabilities.
class Union final {
 public:
 bool Prepare(const runnable_jni::Context&,const native_call_graph_inventory::Inventory&,
  const native_call_graph_replacement::Batch&,const modern_field_metadata::FieldMetadata&,
  const method_identity::Calibration&,const modern_field_metadata::ReadContext&,std::string*);
 bool Validate(const native_source_stop::Session&,const modern_field_metadata::ReadContext&)const noexcept;
 const native_call_graph::Plan&GraphPlan()const noexcept{return plan_;}
 const std::vector<native_call_graph::Node>&Nodes()const noexcept{return nodes_;}
 const std::vector<native_call_graph::Edge>&Edges()const noexcept{return edges_;}
 const std::vector<native_selective_interpretation::Method>&BeforeSelection()const noexcept{return before_selection_;}
 const std::vector<native_selective_interpretation::Method>&ReplacementSelection()const noexcept{return replacement_selection_;}
 private:
 struct RootedNode {
  const bounded_managed_roots::Store*store=nullptr;
  uint64_t generation=0;
  native_selective_interpretation::Method method{};
 };
 struct RootedClass {
  const bounded_managed_roots::Store*store=nullptr;uint64_t generation=0;
  bounded_managed_roots::Slot root{},parent{};
  std::vector<bounded_managed_roots::Slot>interfaces;
  std::vector<size_t>closure;
  uint32_t flags=0;size_t native_parent=SIZE_MAX;
 };
 struct Dispatch {
  native_call_graph::InvokeKind kind=native_call_graph::InvokeKind::Virtual;
  void*declaration=nullptr,*target=nullptr;size_t declaring=SIZE_MAX,receiver=SIZE_MAX;
 };
 bool PrepareDomain(const modern_field_metadata::ReadContext&,std::string*);
 bool CheckDomain(const modern_field_metadata::ReadContext&)const noexcept;
 bool Check(const modern_field_metadata::ReadContext&)const noexcept;
 const native_call_graph_inventory::Inventory*before_=nullptr;
 const native_call_graph_replacement::Batch*replacement_=nullptr;
 const modern_field_metadata::FieldMetadata*metadata_=nullptr;
 std::vector<RootedNode>rooted_,declarations_;
 std::vector<RootedClass>classes_;
 std::vector<Dispatch>dispatches_;
 std::unique_ptr<native_call_graph_dispatch::Reader>dispatch_reader_;
 method_metadata::Layout layout_{};
 // Representative node ordinal for each non-boot loader equivalence class.
 std::vector<size_t>loaders_;
 std::vector<native_call_graph::Node>nodes_;
 std::vector<native_call_graph::Edge>edges_;
 std::vector<native_selective_interpretation::Method>before_selection_,replacement_selection_;
 native_call_graph::Plan plan_{};
 bool attempted_=false,prepared_=false;
};
class Execution final {
 public:
 Execution(const native_source_stop::Service&stop,const native_cache::Service&threads)
  :stop_service_(stop),threads_(threads){}
 Execution(const Execution&)=delete;Execution&operator=(const Execution&)=delete;
 // Ordinary JNI/Runnable source scope only. Calibration may load Classes, so
 // the real independent fresh public census follows all resolution/calibration.
 // input_roots must be the original Verifier::Stage definition-base inputs,
 // not its expanded affected descendants. Fresh census checks that exact set.
 // A supplied actual super resolver must be calibrated/bound before Prepare,
 // hence before its graph resolution and fresh census. Without it, any super
 // edge leaves the graph incomplete and is refused. No owner is calibrated here
 // a second time for super resolution.
 // The original Verified/Collected source booleans are not consumed here.
 bool Prepare(HookEngine&,JNIEnv*,jvmtiEnv*,const std::vector<jclass>&input_roots,
  const runnable_jni::Context&,native_schema_guard_verifier::Verifier&,
  receiver_guard::SourceProducer&,const method_identity::Calibration&,
  const modern_field_metadata::FieldMetadata&,stack_visitor_service::Declaration,
  std::string*error,const native_call_graph_super::Resolver*super_resolver=nullptr);
 // Same original Runnable/source loading operation, after BEFORE inspection
 // and every original FinishClassAllocations. Definitions must cover exactly
 // every Class in graph's structural partitions. Roots and input inventories
 // must outlive this Execution. Calibrations are borrowed from Prepare; no
 // owner calibration is attempted inside the class-loading pause.
 bool PrepareReplacements(HookEngine&,const runnable_jni::Context&,jvmtiEnv*,
  const native_source_class_loading::Run&,const void*driver,
  const native_owned_removal::RootedGraph&,
  const std::vector<native_call_graph_replacement::Definition>&,
  bounded_managed_roots::Store&,std::string*);
 // Exactly one actual scoped inspection. All paths return after Session cleanup
 // has resumed the world. A selected quick/inline carrier is an explicit refusal;
 // no retry, sleep, drain loop, or inference from deoptimized entrypoints exists.
 // After PrepareReplacements this validates/activates new bodies and added
 // old ancestors, then rescans every selection. Body drain and complete union
 // coverage remain separate facts; incomplete union cannot pass ValidateFinal.
 Report InspectActivation(size_t self_extent,const modern_field_metadata::ReadContext&)noexcept;
 // Caller supplies its actual same-operation source pause and current snapshot.
 // FinalDrained covers the prepared BEFORE or complete union selection at
 // this later pause. Incoming identities and dispatch tables are revalidated.
 // Native/root/publication owners remain independent.
 Report ValidateFinal(const native_source_stop::Session&,
  const native_source_class_loading::Run&,const void*actual_driver,
  const modern_field_metadata::ReadContext&)noexcept;
 Report ValidateCollected(const native_source_stop::Session&,
  const native_source_class_loading::Run&,const void*,const modern_field_metadata::ReadContext&)noexcept;
 bool ValidateCollectedProof(const native_source_stop::Session&,
  const modern_field_metadata::ReadContext&)const noexcept;
 // Read-only receipt inside the exact same continuous final source stop.
 // Revalidates identities/tables without allocating or walking threads again.
 bool ValidateFinalProof(const native_source_stop::Session&,
  const modern_field_metadata::ReadContext&)const noexcept;
 // The current collected/final whole-thread walk's immutable cursor stream,
 // borrowed only after its exact same-stop graph/census/selection receipt passes.
 // Native continuation and root ownership remain separate commit obligations.
 const std::vector<native_call_graph_stack::Frame>*ValidatedFrames(
   const native_source_stop::Session&,const modern_field_metadata::ReadContext&)const noexcept;
 // Synchronous original Runtime root stream joined to the collected/final
 // complete walk. No root slot/value is retained or upgraded to publication
 // authority; the full graph receipt is checked before and after the callee.
 native_call_graph_stack::FrameRootReport ObserveFrameRoots(const native_source_stop::Session&,
   const modern_field_metadata::ReadContext&,const native_typed_heap_walk::RootProvenance&)const noexcept;
 // Consume the same complete graph stream; never expose its private Prepared
 // owner or let a calibration sample substitute for this transaction window.
 const native_exception_upcall_boundary::Proof*CaptureUpcallBoundary(
   const native_exception_upcall_boundary::Service&,native_exception_upcall_boundary::Service::CaptureStorage&,
   const native_managed_stack_chain::Chain&,const source_commit::Plan&,
   const native_source_stop::Session&,const modern_field_metadata::ReadContext&)const noexcept;
 // Before original source refund can dispose replacement metadata: requires
 // the same live Holder/Run, a complete walk with no selected quick OR shadow
 // frame, then original removal of this operation's new deoptimized entries
 // and its installed BEFORE/union-ancestor entries in the same actual stop.
 // A failed/partial removal may be retried with these roots still pinned.
 // A foreign retained replacement entry refuses completion while its metadata
 // stays pinned. Owned-set cleanup alone never authorizes Class/cache/GC
 // reclamation; their independent reference proof remains required.
 RefundReport RefundReplacements(const native_source_stop::Session&,
  const native_source_class_loading::Run&,const void*driver,
  const modern_field_metadata::ReadContext&)noexcept;
 // Restore installed old-method entries on a failed operation, including
 // partial BEFORE activation. Their pinned original metadata remains valid;
 // this does not grant disposal of unpublished replacement metadata.
 RefundReport RefundInstalled(const native_source_stop::Session&,
  const modern_field_metadata::ReadContext&)noexcept;
 // The hierarchy Session calls this after its original native views unwind,
 // before private retirement. Opens and closes an independently calibrated
 // inspection stop; no reservation/publication permission escapes the call.
 RefundReport InspectRefund(const native_source_class_loading::Run&,const void*driver,
  size_t self_extent,const modern_field_metadata::ReadContext&)noexcept;
 // BEFORE activation may fail before the original source driver exists.
 // This path is restricted to installed metadata and no active loading Run.
 RefundReport InspectRefundInstalled(size_t self_extent,
  const modern_field_metadata::ReadContext&)noexcept;
 // Ordinary original-thread entry for a failed BEFORE inspection. Captures
 // its own fresh mapping snapshot; no caller may retain one across a safepoint.
 RefundReport RefundBeforeRun()noexcept;
 // Called after prepared retirement/obsolete entry publication, before the
 // heap replacement rewrites root Store slots. Publishes the complete rooted
 // union to persistent interpretation before removing owned native entries,
 // then checks the actual surviving entries. Foreign native entries remain
 // independently owned; obsolete carriers use ART's retirement entry.
 RefundReport ReleaseForCommit(const native_source_stop::Session&,
     const modern_field_metadata::ReadContext&)noexcept;
 // Ordinary Collected preparation, after the complete union inspection.
 // Allocate one publication for all old callers and replacement bodies. The
 // reservation owns opaque keys only; the inventories retain actual roots.
 bool PrepareInterpretation(native_interpretation_registry::Registry&,std::string*);
 // Read-only installed-policy prerequisite for ordinary guard preparation.
 // It does not allocate publication storage or grant a stopped proof.
 bool InterpretationPolicyReady(const native_interpretation_registry::Registry&)const noexcept;
 bool InterpretationPrepared(const modern_field_metadata::ReadContext&)const noexcept;
 // Native journal facts only, safe after original Holder/DEX cleanup. This
 // does not read Store slots, native methods or any source-owned pointer.
 bool CommittedEntriesReleased()const noexcept {
  return phase_==Phase::CommittedReleased&&!selection_.PendingRefund()&&
   !union_selection_.PendingRefund()&&!replacement_selection_.PendingRefund();
 }
 // Native journal routing only; no method, Class or source storage is read.
 bool HasOwnedEntries()const noexcept {
  return selection_.PendingRefund()||union_selection_.PendingRefund()||replacement_selection_.PendingRefund();
 }
 // True only inside this operation's actual inspection or refund callback. The
 // caller may use this as a read-only snapshot guard, never mutation permission.
 bool InspectionStopped()const noexcept {
  return (phase_==Phase::Inspecting||phase_==Phase::ReplacementInspecting||phase_==Phase::Refunding)&&
   inspection_read_&&inspection_stop_&&inspection_stop_->IsStopped(self_);
 }
 const Report&LastReport()const noexcept{return report_;}
 const native_call_graph_inventory::Inventory&Inventory()const noexcept{return inventory_;}
 const native_selective_interpretation::Service&Selection()const noexcept{return selection_;}
 const native_call_graph_replacement::Batch&Replacements()const noexcept{return replacements_;}
 const Union&UnionGraph()const noexcept{return union_;}
 private:
 friend class SourceRefund;
 enum class Phase {Empty,Preparing,Prepared,Inspecting,Inspected,
  ReplacementPreparing,ReplacementPrepared,ReplacementInspecting,ReplacementInspected,CollectedReady,Finalized,CommittedReleased,Refunding,Refused};
 Report ValidateWindow(const native_source_stop::Session&,const native_source_class_loading::Run&,
  const void*,const modern_field_metadata::ReadContext&,bool final)noexcept;
 bool ValidateWindowProof(const native_source_stop::Session&,const modern_field_metadata::ReadContext&,bool final)const noexcept;
 static bool Inspect(void*,native_source_stop::Session&)noexcept;
 bool SameGeneration()const noexcept;
 bool CheckInterpretation(const modern_field_metadata::ReadContext&,bool obsolete=false)const noexcept;
 bool RefundOriginalEntries(const native_source_stop::Session&,
  const modern_field_metadata::ReadContext&,RefundReport&)noexcept;
 RefundReport InspectRefundFor(const native_source_class_loading::Run*,const void*driver,
  size_t,const modern_field_metadata::ReadContext&)noexcept;
 RefundReport InspectRefundFresh(const native_source_class_loading::Run*,const void*)noexcept;
 bool CanInspectRefund(const native_source_class_loading::Run*,const void*)const noexcept;
 bool RefusePrepare(Status,const char*,std::string*);
 Report Refuse(Status,const char*)noexcept;
 const native_source_stop::Service&stop_service_;const native_cache::Service&threads_;
 native_call_graph_inventory::Inventory inventory_;
 native_selective_interpretation::Service selection_;
 native_call_graph_stack::Service stack_;
 native_call_graph_stack::Service::Prepared stack_prepared_;
 native_call_graph_replacement::Batch replacements_;
 Union union_;
 native_selective_interpretation::Service replacement_selection_,union_selection_;
 native_interpretation_registry::Registry*interpretation_=nullptr;
 std::unique_ptr<native_interpretation_registry::Registry::Reservation>interpretation_reservation_;
 std::vector<void*>interpretation_methods_;
 std::vector<native_selective_interpretation::Method>union_extra_;
 native_call_graph_stack::Service::Prepared replacement_stack_,union_stack_;
 const method_identity::Calibration*methods_=nullptr;
 const modern_field_metadata::FieldMetadata*metadata_=nullptr;
 receiver_guard::SourceProducer*source_=nullptr;
 native_schema_guard_verifier::Verifier*verifier_=nullptr;
 const native_call_graph_super::Resolver*super_resolver_=nullptr;
 const bounded_managed_roots::Store*roots_=nullptr;uint64_t generation_=0;
 void*self_=nullptr;const modern_field_metadata::ReadContext*inspection_read_=nullptr;
 const native_source_stop::Session*inspection_stop_=nullptr;
 const native_source_stop::Session*final_stop_=nullptr;
 const modern_field_metadata::ReadContext*final_read_=nullptr;
 Phase phase_=Phase::Empty;Report report_{};
};
// Concrete before_private_discard callback for the hierarchy Session. The
// synchronous Run owner keeps this adapter, Execution and loading Run alive
// until original cleanup completes. No mapping snapshot crosses a callback.
class SourceRefund final {
 public:
 SourceRefund(Execution&execution,const native_source_class_loading::Run&loading,const void*driver)
  :execution_(execution),loading_(loading),driver_(driver){}
 SourceRefund(const SourceRefund&)=delete;SourceRefund&operator=(const SourceRefund&)=delete;
 static bool BeforePrivateDiscard(void*owner,const char**refusal)noexcept;
 // Exact same-source binding only; refuses to overwrite another refund owner.
 bool Bind(native_hierarchy_session::Bindings&)noexcept;
 const RefundReport&LastReport()const noexcept{return report_;}
 private:
 Execution&execution_;const native_source_class_loading::Run&loading_;const void*driver_;
 RefundReport report_{};bool running_=false;
};
} // namespace artdcevm::native_call_graph_execution
