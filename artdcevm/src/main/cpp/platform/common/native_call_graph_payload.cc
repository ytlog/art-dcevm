#include "platform/common/native_call_graph_payload.h"
#include "platform/common/native_schema_binding.h"

namespace artdcevm::native_call_graph_payload {
bool Payload::Prepare(const runnable_jni::Context&context,
 const native_owned_removal::RootedGraph&graph,native_call_graph_run::Owner&run,
 const Bindings&bindings,prepared_roots::Registry&registry,std::string*error){
 const auto fail=[&](const char*why){if(error)*error=why;return false;};
 if(attempted_)return fail("call-graph-payload-preparation-repeated");
 attempted_=true;
 auto deps=bindings.commit;
 if(!run.Running()||!run.Driver()||!run.Loading()||!run.Stop()||!run.Hierarchy()||
    !run.Ledger()||!run.CommitOwner()||run.CommitOwner()->PreparedService()||
    !context.env||!context.vm||!context.self||!runnable_jni::Bridge::BorrowOriginalScope(context)||
    !native_source_stop::Session::InspectionAvailable()||run.Stop()->Reserved()||
    native_source_class_loading::Run::Current()!=run.Loading()||!run.Loading()->Paused()||
    !graph.holder||!run.Loading()->OwnsFinishedHolder(graph.holder,graph.metadata.count)||
    !deps.metadata||!deps.metadata->Calibrated()||!deps.fields||!deps.fields->Calibrated()||
    !deps.schema||!deps.execution||!deps.interpretation||!deps.read||!deps.read->owner||!deps.read->extent||
    deps.reflection_plan||deps.initialization||!deps.methods.empty()||
    !bindings.final_fields||!bindings.final_fields->UsesMetadata(*deps.metadata)||!bindings.hooks||!bindings.method_model||bindings.method_model->identities.size()<3||
    !bindings.method_binding||!bindings.method_retired||
    !registry.NativeInsertionReady()||!bindings.authority||!bindings.class_linker||
    !run.Hierarchy()->OwnsAuthority(*bindings.authority)||
    !deps.execution->Replacements().OwnsSource(*run.Loading(),run.Driver(),graph.holder)||
    deps.execution->LastReport().status!=native_call_graph_execution::Status::ReplacementInspectedDrained||
    !deps.execution->LastReport().union_complete)
  return fail("call-graph-payload-collected-preparation-owner-refused");
 if(!deps.execution->Replacements().ValidateOrdinary(*deps.read)||
    !rooted_schema_plan::Read(graph,true,bindings.reader,&plan_,error))return false;
 if(plan_.old_classes.size()!=deps.execution->Replacements().ClassCount())
  return fail("call-graph-payload-complete-Holder-class-count-refused");
 methods_.reserve(plan_.old_classes.size());method_views_.reserve(plan_.old_classes.size());
 policies_.reserve(plan_.old_classes.size());deps.methods.reserve(plan_.old_classes.size());
 for(size_t i=0;i<plan_.old_classes.size();++i){
  evolution::ClassSchema before,after;native_method_table::View old_table,new_table;
  if(!native_schema_policy::ReadNativeDeclarations(*deps.metadata,*bindings.method_model,
       plan_.old_classes[i],&before,error,&old_table,deps.read)||
     !native_schema_policy::ReadNativeDeclarations(*deps.metadata,*bindings.method_model,
       plan_.new_classes[i],&after,error,&new_table,deps.read))return false;
  native_schema_policy::Plan policy;
  if(!native_schema_policy::Plan::Prepare(before,after,&policy,error))return false;
  auto method=modern_methods::Service::PrepareTables(*bindings.method_binding,
    plan_.old_classes[i],old_table.methods,plan_.new_classes[i],new_table.methods,
    bindings.method_retired,error,deps.read);
  if(!method)return false;
  method_views_.push_back(method.get());deps.methods.push_back(method.get());
  methods_.push_back(std::move(method));policies_.push_back(std::move(policy));
 }
 if(!bindings.final_fields->Bind(context,*run.Stop(),plan_,deps.methods,*deps.read,error))return false;
 std::vector<native_schema_policy::GuardRequest>requests;
 if(!native_schema_policy::InspectNative(plan_,*deps.metadata,*bindings.method_model,
      method_views_,policies_,&requests,error,deps.read)||
    !deps.schema->Seal(plan_,error)||!deps.schema->PrepareGuardProofs(plan_,requests,error)||
    !bindings.authority->PrepareRoots(plan_,error)||!run.Hierarchy()->PrepareCollected(plan_,error))return false;
 const native_structural_reflection::ResolutionBinding resolution{
   bindings.hooks,bindings.method_model,deps.fields};
 // Native policy inspection is complete, while hierarchy permissions are
 // intentionally deferred to SourceOwner's real stopped authority. The
 // ordinary Plan::Prepare entry still requires every immediate guard.
 reflection_=native_structural_reflection::Plan::PrepareImpl(plan_,*deps.metadata,
   method_views_,bindings.retirement,error,bindings.handles,resolution,&policies_,
   deps.schema,deps.read,false);
 if(!reflection_)return false;
 if(!initialization_.Prepare(plan_,*deps.metadata,*deps.fields,deps.read))
  return fail("call-graph-payload-initialization-transfer-refused");
 if(!rooted_schema_plan::Matches(graph,bindings.reader,plan_)||
    !deps.execution->Replacements().ValidateOrdinary(*deps.read))
  return fail("call-graph-payload-native-Holder-cohort-changed");
 deps.reflection_plan=reflection_.get();deps.initialization=&initialization_;
 deps.loading=run.Loading();deps.stop=run.Stop();deps.self=context.self;
 deps.compilation=run.Compilation();deps.compilation_receipt=run.CompilationReceipt();
 deps.driver=run.Driver();deps.holder=graph.holder;
 auto service=native_call_graph_commit::Service::Prepare(context,plan_,deps,error);
 if(!service||!run.CommitOwner()->Bind(std::move(service),error)||
    !run.CommitOwner()->PreparePublication(context,*run.Ledger(),bindings.class_linker,error)||
    !run.CommitOwner()->RegisterRoots(context,registry,*bindings.authority,*run.Ledger(),error))return false;
 prepared_=true;return true;
}
}
