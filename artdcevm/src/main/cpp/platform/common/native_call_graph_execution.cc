#include "platform/common/native_call_graph_execution.h"

namespace artdcevm::native_call_graph_execution {
bool Execution::SameGeneration()const noexcept {
 return roots_&&inventory_.RootStore()==roots_&&roots_->Generation()==generation_;
}
bool Execution::RefusePrepare(Status status,const char*failure,std::string*error){
 phase_=Phase::Refused;report_.status=status;report_.failure=failure;
 report_.compiled_drained=false;report_.replacement_bodies_drained=false;
 if(error&&error->empty())*error=failure;return false;
}
Report Execution::Refuse(Status status,const char*failure)noexcept {
 phase_=Phase::Refused;report_.status=status;report_.failure=failure;
 report_.compiled_drained=false;report_.replacement_bodies_drained=false;return report_;
}
bool Execution::Prepare(HookEngine&hooks,JNIEnv*env,jvmtiEnv*ti,
 const std::vector<jclass>&input_roots,const runnable_jni::Context&context,
 native_schema_guard_verifier::Verifier&verifier,receiver_guard::SourceProducer&source,
 const method_identity::Calibration&methods,const modern_field_metadata::FieldMetadata&metadata,
 stack_visitor_service::Declaration declaration,std::string*error,
 const native_call_graph_super::Resolver*super_resolver){
 if(phase_!=Phase::Empty||!env||!ti||context.env!=env||!context.self||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||native_source_class_loading::Run::Current())
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-original-ordinary-operation-unavailable",error);
 phase_=Phase::Preparing;self_=context.self;
 if(!selection_.CalibrateOwner(hooks,env,error)||!replacement_selection_.CalibrateOwner(hooks,env,error)||
    !inventory_.Prepare(context,verifier,source,error)||
    !(super_resolver?inventory_.ResolveCalls(context,source,*super_resolver,error):
                     inventory_.ResolveCalls(context,source,error))||
    !verifier.RefreshCallGraphCensus(env,ti,input_roots,error))
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-real-inventory-resolution-census-refused",error);
 roots_=inventory_.RootStore();generation_=roots_?roots_->Generation():0;
 if(!SameGeneration())return RefusePrepare(Status::PreparationRefused,"call-graph-execution-root-generation-unavailable",error);
 report_.graph_native_boundaries=inventory_.GraphPlan().native_boundaries.size();
 report_.native_boundary_obligations=report_.graph_native_boundaries!=0;
 if(!inventory_.GraphPlan().complete)
  return RefusePrepare(Status::UnsupportedIncompleteGraph,"call-graph-execution-complete-callgraph-unavailable",error);
 const auto&selected=inventory_.Selection();
 if(selected.empty())return RefusePrepare(Status::UnsupportedEmptySelection,"call-graph-execution-empty-managed-selection",error);
 if(!selection_.Prepare(hooks,methods,metadata,*roots_,selected.data(),selected.size(),error)||
    !stack_.Bind(hooks,declaration,error)||!stack_.Prepare(selection_,&stack_prepared_,error)||!SameGeneration())
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-real-selection-stack-preparation-refused",error);
 methods_=&methods;metadata_=&metadata;source_=&source;
 phase_=Phase::Prepared;report_.status=Status::Prepared;report_.failure=nullptr;return true;
}
bool Execution::PrepareReplacements(HookEngine&hooks,const runnable_jni::Context&context,jvmtiEnv*ti,
 const native_source_class_loading::Run&loading,const void*driver,
 const native_owned_removal::RootedGraph&graph,
 const std::vector<native_call_graph_replacement::Definition>&definitions,
 bounded_managed_roots::Store&roots,std::string*error){
 if(phase_!=Phase::Inspected||!SameGeneration()||context.self!=self_||!methods_||!metadata_||!source_)
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-replacement-order-or-owner-refused",error);
 phase_=Phase::ReplacementPreparing;report_.compiled_drained=false;
 if(!replacements_.Stage(hooks,context,ti,loading,driver,graph,definitions,roots,*metadata_,*methods_,*source_,error))
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-complete-replacement-binding-refused",error);
 const auto&selected=replacements_.Selection();
 if(!selected.empty()&&(!replacement_selection_.Prepare(hooks,*methods_,*metadata_,roots,selected.data(),selected.size(),error)||
                       !replacement_selection_.PrepareRefund(hooks,error)||
                       !stack_.Prepare(replacement_selection_,&replacement_stack_,error)))
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-replacement-selection-stack-refused",error);
 if(!SameGeneration())return RefusePrepare(Status::PreparationRefused,"call-graph-execution-before-roots-changed-during-replacement-stage",error);
 report_.scope=GraphScope::InstalledBeforeAndReplacementBodies;
 report_.replacement_classes=replacements_.ClassCount();
 phase_=Phase::ReplacementPrepared;report_.status=Status::ReplacementPrepared;report_.failure=nullptr;return true;
}
bool Execution::Inspect(void*raw,native_source_stop::Session&stop)noexcept {
 auto&operation=*static_cast<Execution*>(raw);
 const auto deny=[&](const char*failure){operation.report_.failure=failure;return false;};
 const bool replacement=operation.phase_==Phase::ReplacementInspecting;
 if((operation.phase_!=Phase::Inspecting&&!replacement)||!operation.inspection_read_||
    !stop.IsStopped(operation.self_)||!operation.SameGeneration())
  return deny("call-graph-execution-current-inspection-owner-generation-refused");
 operation.inspection_stop_=&stop;
 struct CallbackScope {
  Execution&operation;
  ~CallbackScope(){operation.inspection_stop_=nullptr;}
 }callback_scope{operation};
 const auto&read=*operation.inspection_read_;
 if(!operation.inventory_.ValidateOrdinary(stop,read))
  return deny("call-graph-execution-current-ordinary-inventory-refused");
 if(replacement&&!operation.replacements_.Validate(stop,read))
  return deny("call-graph-execution-current-replacement-batch-refused");
 native_selective_interpretation::Report retained;
 if(replacement&&!operation.selection_.ValidateRetained(stop,operation.self_,read,&retained))
  return deny(retained.failure?retained.failure:"call-graph-execution-before-retained-selection-refused");
 if(!replacement&&!operation.selection_.Activate(stop,operation.self_,read,&operation.report_.activation))
  return deny(operation.report_.activation.failure?operation.report_.activation.failure:
              "call-graph-execution-real-selective-activation-refused");
 if(replacement&&!operation.replacements_.Selection().empty()&&
    !operation.replacement_selection_.Activate(stop,operation.self_,read,&operation.report_.replacement_activation))
  return deny(operation.report_.replacement_activation.failure?operation.report_.replacement_activation.failure:
              "call-graph-execution-real-replacement-activation-refused");
 auto&before=replacement?operation.report_.before_reinspected:operation.report_.inspected;
 before=operation.stack_.Scan(stop,operation.self_,operation.threads_,operation.stack_prepared_,read);
 if(!before.walk_complete)
  return deny(before.failure?before.failure:
              "call-graph-execution-complete-inspection-walk-refused");
 if(replacement&&!operation.replacements_.Selection().empty()){
  auto&after=operation.report_.replacement_inspected;
  after=operation.stack_.Scan(stop,operation.self_,operation.threads_,operation.replacement_stack_,read);
  if(!after.walk_complete)return deny(after.failure?after.failure:"call-graph-execution-complete-replacement-walk-refused");
 }
 if(replacement&&(!operation.selection_.ValidateRetained(stop,operation.self_,read,&retained)||
    (!operation.replacements_.Selection().empty()&&
     !operation.replacement_selection_.ValidateRetained(stop,operation.self_,read,&retained))))
  return deny(retained.failure?retained.failure:"call-graph-execution-postinspection-retained-selections-refused");
 if(!operation.SameGeneration()||!operation.inventory_.ValidateOrdinary(stop,read)||
    (replacement&&!operation.replacements_.Validate(stop,read))||
    !stop.IsStopped(operation.self_))
  return deny("call-graph-execution-postinspection-inventory-owner-changed");
 return true;
}
Report Execution::InspectActivation(size_t self_extent,
 const modern_field_metadata::ReadContext&read)noexcept {
 const bool replacement=phase_==Phase::ReplacementPrepared;
 if((phase_!=Phase::Prepared&&!replacement)||!SameGeneration()||!read.owner||!read.extent||!self_extent)
  return Refuse(Status::InspectionRefused,"call-graph-execution-inspection-order-or-read-generation-refused");
 phase_=replacement?Phase::ReplacementInspecting:Phase::Inspecting;
 inspection_read_=&read;bool inspected=false,cleanup_complete=false;
 {
  native_source_stop::Session stop(stop_service_);
  inspected=stop.Inspect(self_,self_extent,this,Inspect);
  cleanup_complete=!stop.Reserved()&&!stop.IsStopped(self_);
  if(!inspected&&!report_.failure)report_.failure=stop.Failure();
 }
 // The stack owner is destroyed before returning, including failed inspection.
 inspection_read_=nullptr;
 if(!inspected||!cleanup_complete||!SameGeneration())
  return Refuse(Status::InspectionRefused,report_.failure?report_.failure:
                "call-graph-execution-inspection-resume-or-generation-refused");
 const auto&before=replacement?report_.before_reinspected:report_.inspected;
 const auto&after=report_.replacement_inspected;
 report_.native_boundary_obligations|=before.boundary_owners_required||after.boundary_owners_required;
 if(before.drain_required||!before.selected_drained||
    (replacement&&!replacements_.Selection().empty()&&(after.drain_required||!after.selected_drained)))
  return Refuse(Status::UnsupportedActiveCompiled,
                "call-graph-execution-active-selected-compiled-frame-cannot-be-promised-to-drain");
 phase_=replacement?Phase::ReplacementInspected:Phase::Inspected;
 report_.status=replacement?Status::ReplacementInspectedDrained:Status::InspectedDrained;
 report_.replacement_bodies_drained=replacement;
 // The union graph may add interpreted ancestors/targets beyond these bodies.
 report_.compiled_drained=!replacement;report_.failure=nullptr;return report_;
}
Report Execution::ValidateFinal(const native_source_stop::Session&stop,
 const native_source_class_loading::Run&loading,const void*driver,
 const modern_field_metadata::ReadContext&read)noexcept {
 if(phase_==Phase::ReplacementInspected)
  return Refuse(Status::UnsupportedIncompleteGraph,"call-graph-execution-replacement-union-resolution-closure-unavailable");
 if(phase_!=Phase::Inspected||!SameGeneration()||!stop.IsStopped(self_)||
    !loading.Paused()||!driver||!loading.OwnsDriver(driver)||!read.owner||!read.extent)
  return Refuse(Status::FinalRefused,"call-graph-execution-final-source-owner-order-generation-refused");
 if(!inventory_.SealLoading(loading,driver,read,stop)||!inventory_.Validate(read,stop))
  return Refuse(Status::FinalRefused,"call-graph-execution-final-source-census-inventory-refused");
 native_selective_interpretation::Report retained;
 if(!selection_.ValidateRetained(stop,self_,read,&retained))
  return Refuse(Status::FinalRefused,retained.failure?retained.failure:
                "call-graph-execution-final-retained-selection-refused");
 report_.final=stack_.Scan(stop,self_,threads_,stack_prepared_,read);
 report_.native_boundary_obligations|=report_.final.boundary_owners_required;
 if(!report_.final.walk_complete||!SameGeneration()||!inventory_.Validate(read,stop)||!stop.IsStopped(self_))
  return Refuse(Status::FinalRefused,report_.final.failure?report_.final.failure:
                "call-graph-execution-final-complete-walk-inventory-refused");
 if(report_.final.drain_required||!report_.final.selected_drained)
  return Refuse(Status::UnsupportedActiveCompiled,
                "call-graph-execution-final-selected-compiled-frame-active");
 phase_=Phase::Finalized;report_.status=Status::FinalDrained;
 report_.compiled_drained=true;report_.failure=nullptr;return report_;
}
RefundReport Execution::RefundReplacements(const native_source_stop::Session&stop,
 const native_source_class_loading::Run&loading,const void*driver,
 const modern_field_metadata::ReadContext&read)noexcept{
 RefundReport result;
 result.entries.selected=replacement_selection_.SelectedCount();
 result.entries.pending=replacement_selection_.PendingRefund();
 const auto deny=[&](const char*why){result.failure=why;return result;};
 // Refused operations keep their replacement bindings/journal. Never use the
 // derived readiness flags as a lifetime or cleanup capability.
 if(!replacements_.ClassCount()||phase_==Phase::ReplacementPreparing||
    phase_==Phase::Inspecting||phase_==Phase::ReplacementInspecting||phase_==Phase::Finalized||
    native_source_class_loading::Run::Current()!=&loading||!loading.OwnsDriver(driver)||
    !stop.IsStopped(self_)||!replacements_.Validate(stop,read))
  return deny("call-graph-execution-refund-current-replacement-Holder-owner-refused");
 if(!replacements_.Selection().empty()){
  result.inspected=stack_.ScanForRefund(stop,self_,threads_,replacement_stack_,read);
  if(!result.inspected.walk_complete)return deny(result.inspected.failure?result.inspected.failure:
   "call-graph-execution-refund-complete-thread-walk-refused");
  if(result.inspected.selected_quick||result.inspected.selected_shadow)
   return deny("call-graph-execution-refund-replacement-frame-still-retains-method");
  if(!replacements_.Validate(stop,read)||!replacement_selection_.Refund(stop,self_,read,&result.entries))
   return deny(result.entries.failure?result.entries.failure:"call-graph-execution-refund-owned-entry-removal-refused");
 }else result.entries.complete=true;
 if(!stop.IsStopped(self_)||!replacements_.Validate(stop,read))
  return deny("call-graph-execution-refund-postremoval-Holder-owner-changed");
 phase_=Phase::Refused;report_.compiled_drained=false;report_.replacement_bodies_drained=false;
 report_.status=Status::FinalRefused;
 report_.failure="call-graph-execution-replacement-entries-refunded-no-commit";
 result.owned_entries_removed=true;return result;
}
} // namespace artdcevm::native_call_graph_execution
