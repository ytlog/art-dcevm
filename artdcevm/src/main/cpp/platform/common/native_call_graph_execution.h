#pragma once
#include "platform/common/native_call_graph_inventory.h"
#include "platform/common/native_call_graph_stack.h"

namespace artdcevm::native_call_graph_execution {
// One synchronous operation over the installed BEFORE graph. Incoming DEX,
// replacement Classes and newly allocated ArtMethods are not covered. A future
// mutation owner must independently retain/revalidate their union selection.
// These results describe installed selected compiled carriers and independent
// native-boundary obligations, never schema completion, GC/publication rights
// or future interpreter coverage of replacement methods.
enum class Status {
 Empty, Prepared, PreparationRefused, UnsupportedIncompleteGraph,
 UnsupportedEmptySelection, InspectionRefused, UnsupportedActiveCompiled,
 InspectedDrained, FinalRefused, FinalDrained
};
enum class GraphScope {InstalledBeforeOnly};
struct Report {
 GraphScope scope=GraphScope::InstalledBeforeOnly;
 Status status=Status::Empty;
 native_selective_interpretation::Report activation{};
 native_call_graph_stack::Report inspected{},final{};
 size_t graph_native_boundaries=0;
 bool compiled_drained=false,native_boundary_obligations=false;
 const char*failure=nullptr;
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
 // Exactly one actual scoped inspection. All paths return after Session cleanup
 // has resumed the world. A selected quick/inline carrier is an explicit refusal;
 // no retry, sleep, drain loop, or inference from deoptimized entrypoints exists.
 Report InspectActivation(size_t self_extent,const modern_field_metadata::ReadContext&)noexcept;
 // Caller supplies its actual same-operation source pause and current snapshot.
 // FinalDrained means only the same installed-before selection is drained at
 // this later pause. It does not attest incoming DEX/new Method interpretation.
 // Native/root/publication owners remain independent.
 Report ValidateFinal(const native_source_stop::Session&,
  const native_source_class_loading::Run&,const void*actual_driver,
  const modern_field_metadata::ReadContext&)noexcept;
 // True only inside this operation's private actual Inspect callback. The
 // caller may use this as a read-only snapshot guard, never mutation permission.
 bool InspectionStopped()const noexcept {
  return phase_==Phase::Inspecting&&inspection_stop_&&inspection_stop_->IsStopped(self_);
 }
 const Report&LastReport()const noexcept{return report_;}
 const native_call_graph_inventory::Inventory&Inventory()const noexcept{return inventory_;}
 const native_selective_interpretation::Service&Selection()const noexcept{return selection_;}
 private:
 enum class Phase {Empty,Preparing,Prepared,Inspecting,Inspected,Finalized,Refused};
 static bool Inspect(void*,native_source_stop::Session&)noexcept;
 bool SameGeneration()const noexcept;
 bool RefusePrepare(Status,const char*,std::string*);
 Report Refuse(Status,const char*)noexcept;
 const native_source_stop::Service&stop_service_;const native_cache::Service&threads_;
 native_call_graph_inventory::Inventory inventory_;
 native_selective_interpretation::Service selection_;
 native_call_graph_stack::Service stack_;
 native_call_graph_stack::Service::Prepared stack_prepared_;
 const bounded_managed_roots::Store*roots_=nullptr;uint64_t generation_=0;
 void*self_=nullptr;const modern_field_metadata::ReadContext*inspection_read_=nullptr;
 const native_source_stop::Session*inspection_stop_=nullptr;
 Phase phase_=Phase::Empty;Report report_{};
};
} // namespace artdcevm::native_call_graph_execution
