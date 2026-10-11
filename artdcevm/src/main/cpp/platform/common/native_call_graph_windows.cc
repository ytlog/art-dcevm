#include "platform/common/native_call_graph_windows.h"
#include "platform/common/redefinition_gate.h"
#include "platform/common/owned_failure_diagnostic.h"
#include "platform/common/schema_snapshot.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace artdcevm::native_call_graph_windows {
Factory::Factory(native_call_graph_run::Owner&owner,const Bindings&bindings)
 :owner_(owner),run_(owner.bindings_),bindings_(bindings),roots_(bindings.schema?bindings.schema->CallGraphRoots():nullptr),
  generation_(roots_?roots_->Generation():0){bindings_.verifier.access_permissions=this;}
Factory::~Factory(){
 // Payload addresses are borrowed by the request's final commit adapter.
 // Original end_run closes that adapter's source views before this teardown.
 if(owner_.SourceViewsLive())std::abort();
 // An untouched owner is still responsible for a failed BEFORE activation.
 // Close it before destroying any of this factory's borrowed payload storage.
 owner_.CloseBeforeRun();
}
jvmtiError Factory::Apply(jvmtiEnv*ti,const std::vector<Definition>&definitions,std::string*error){
 if(apply_attempted_||!Ready()||!ti||definitions.empty()||
    run_.hierarchy.context->env->ExceptionCheck()||
    !runnable_jni::Bridge::BorrowOriginalScope(*run_.hierarchy.context)){
  owned_failure_diagnostic::Publish("call-graph-transaction-operation-owner-or-input-refused",error);
  return JVMTI_ERROR_NOT_AVAILABLE;
 }
 apply_attempted_=true;
 // Keep the operation lease through native owner closure and root cleanup.
 // A startup witness is required even for a single structural target: its
 // loaded-caller graph can span Classes outside the submitted batch.
 redefinition_gate::Lease lease(static_cast<JavaVM*>(run_.hierarchy.context->vm));
 struct Close{Factory&factory;~Close(){factory.Finish();}}close{*this};
 if(!lease.Acquired()){
  owned_failure_diagnostic::Publish(redefinition_gate::Diagnostic(),error);
  return JVMTI_ERROR_NOT_AVAILABLE;
 }
 try{
  native_owned_removal::Provider provider;
  if(!Attach(&provider,error))return JVMTI_ERROR_NOT_AVAILABLE;
  return native_owned_removal::RunBatch(*run_.hierarchy.hooks,run_.hierarchy.context->env,ti,definitions,provider,error);
 }catch(const std::bad_alloc&){
  owned_failure_diagnostic::Publish("call-graph-transaction-preparation-allocation-failed",error);
  return JVMTI_ERROR_OUT_OF_MEMORY;
 }
}
void Factory::Finish()noexcept{
 // A live original Run must have completed its own end_run callback. This
 // closes only an unentered operation or an already closed native lifetime;
 // it cannot turn an in-flight publication into an ordinary refund.
 owner_.CloseBeforeRun();
 if(owner_.SourceViewsLive()||run_.execution->HasOwnedEntries())std::abort();
 native_read_=false;mappings_.reset();
 registry_.Clear();
 record_.reset();inputs_.clear();incoming_.clear();submitted_incoming_.clear();
 run_.hierarchy.authority->Clear(run_.hierarchy.context->env);
 bindings_.schema->Clear(run_.hierarchy.context->env);
 phase_=Phase::Refused;
}
bool Factory::Ready()const noexcept{
 return phase_==Phase::Prepared&&run_.hierarchy.hooks&&roots_&&generation_&&
  roots_->Generation()==generation_&&bindings_.schema&&run_.execution&&
  run_.self_extent&&run_.hierarchy.context&&run_.hierarchy.context->env&&
  run_.hierarchy.context->vm&&run_.hierarchy.context->self&&
  bindings_.source&&bindings_.installed&&bindings_.stack_declaration&&bindings_.payload.method_model&&
  run_.hierarchy.pool&&run_.hierarchy.authority&&
  bindings_.payload.authority==run_.hierarchy.authority&&
  run_.hierarchy.metadata&&bindings_.payload.commit.metadata==run_.hierarchy.metadata&&
  bindings_.verifier.vm==run_.hierarchy.context->vm&&
  bindings_.verifier.metadata==run_.hierarchy.metadata&&
  bindings_.verifier.methods==bindings_.payload.method_model&&
  bindings_.hierarchy.metadata==run_.hierarchy.metadata&&
  bindings_.hierarchy.allocation==run_.hierarchy.allocation&&
  bindings_.payload.commit.schema==bindings_.schema&&
  bindings_.payload.commit.execution==run_.execution;
}
bool Factory::BeforePrepared()const noexcept{
 return Ready()&&inputs_staged_&&!inputs_.empty()&&
  run_.execution->Inventory().RootStore()==roots_&&
  run_.execution->LastReport().status==native_call_graph_execution::Status::InspectedDrained;
}
bool Factory::MatchInputs(JNIEnv*env,const std::vector<Definition>&definitions,
 const std::vector<RootedInput>&inputs,std::string*error)const{
 const auto fail=[&](const char*why){if(error)*error=why;return false;};
 if(!run_.hierarchy.context||!env||env!=run_.hierarchy.context->env||env->ExceptionCheck()||!roots_||
    roots_->Generation()!=generation_||definitions.empty())
  return fail("call-graph-windows-public-input-owner-refused");
 std::vector<bool>seen(inputs.size());size_t submitted=0;
 // JNI identity is used here deliberately. Raw Class addresses must not be
 // carried across these calls, which can safepoint and move either identity.
 for(size_t i=0;i<inputs.size();++i){const auto&input=inputs[i];
  if(!roots_->Valid(input.original)||!input.incoming||!input.incoming->Schema())
   return fail("call-graph-windows-independent-input-not-staged");
  submitted+=input.submitted;
  for(size_t j=0;j<i;++j){
   jobject previous=roots_->LocalRef(env,inputs[j].original);
   if(!previous||env->ExceptionCheck()){
    if(previous)env->DeleteLocalRef(previous);
    return fail("call-graph-windows-independent-input-root-lost");
   }
   const bool duplicate=roots_->Same(env,input.original,previous);
   env->DeleteLocalRef(previous);
   if(duplicate||env->ExceptionCheck())return fail("call-graph-windows-independent-input-Class-duplicate");
  }
 }
 if(submitted!=definitions.size())return fail("call-graph-windows-submitted-input-count-mismatch");
 for(const auto&definition:definitions){size_t found=SIZE_MAX;
  if(!definition.klass||definition.dex.empty())return fail("call-graph-windows-null-submitted-input");
  for(size_t i=0;i<inputs.size();++i){const auto&input=inputs[i];
   if(!input.submitted)continue;
   const bool same=roots_->Same(env,input.original,definition.klass);
   if(env->ExceptionCheck())return fail("call-graph-windows-submitted-input-Class-read-failed");
   if(same){if(found!=SIZE_MAX)return fail("call-graph-windows-submitted-input-Class-ambiguous");found=i;}
  }
  if(found==SIZE_MAX||seen[found]||!inputs[found].incoming->MatchesInput(definition.dex))
   return fail("call-graph-windows-submitted-input-Class-or-independent-DEX-mismatch");
  seen[found]=true;
 }
 for(size_t i=0;i<seen.size();++i)if(inputs[i].submitted&&!seen[i])
  return fail("call-graph-windows-submitted-input-omitted");
 return roots_->Generation()==generation_&&!env->ExceptionCheck();
}
bool Factory::Stage(JNIEnv*env,jvmtiEnv*ti,const std::vector<Definition>&definitions,std::string*error){
 if(!Ready()||!env||env!=run_.hierarchy.context->env||!ti||definitions.empty()||
    !submitted_incoming_.empty()||run_.hierarchy.authority->Staged()){
  if(error)*error="call-graph-windows-final-definition-stage-order-refused";return false;
 }
 struct Globals{JNIEnv*env;std::vector<jclass>values;~Globals(){for(auto value:values)env->DeleteGlobalRef(value);}}globals{env,{}};
 std::vector<native_hierarchy_graph::Input>proposals;
 std::vector<jclass>classes;
 globals.values.reserve(definitions.size());proposals.reserve(definitions.size());classes.reserve(definitions.size());
 submitted_incoming_.reserve(definitions.size());
 for(const auto&definition:definitions){
  native_hierarchy_graph::Input proposal;
  if(live_schema::ReadLiveSchema(ti,env,definition.klass,&proposal.before,error)!=JVMTI_ERROR_NONE||env->ExceptionCheck())return false;
  auto incoming=std::make_unique<native_call_graph_incoming::Inventory>();
  if(!incoming->Capture(definition.dex,proposal.before.descriptor,error)||!incoming->Schema())return false;
  proposal.after=*incoming->Schema();
  auto global=static_cast<jclass>(env->NewGlobalRef(definition.klass));
  if(!global||env->ExceptionCheck()){if(global)env->DeleteGlobalRef(global);return false;}
  globals.values.push_back(global);proposal.strong_original=global;
  classes.push_back(global);proposals.push_back(std::move(proposal));submitted_incoming_.push_back(std::move(incoming));
 }
 // Resolve proposed relations before closing the verifier's loaded-caller
 // census. Both owners receive the same exact callback Context and final DEX.
 if(!run_.hierarchy.authority->Stage(*run_.hierarchy.context,ti,proposals,bindings_.hierarchy,error)||
    !bindings_.schema->Stage(*run_.hierarchy.context,ti,classes,bindings_.verifier,error))return false;
 struct Locals{JNIEnv*env;std::vector<jclass>values;~Locals(){for(auto value:values)env->DeleteLocalRef(value);}}affected{env,{}};
 const auto*graph=run_.hierarchy.authority->Staged();if(!graph)return false;
 affected.values.reserve(graph->nodes.size());
 for(const auto&node:graph->nodes)if(node.affected){
  if(!node.class_root.Valid()){
   if(error)*error="call-graph-windows-hierarchy-affected-root-lost";return false;
  }
  auto local=static_cast<jclass>(node.class_root.owner->LocalRef(env,node.class_root.slot));
  if(!local||env->ExceptionCheck()){if(local)env->DeleteLocalRef(local);return false;}
  affected.values.push_back(local);
 }
 // Public member staging can load another descendant. Reject before original
 // Run if it changed the complete affected set independently rooted above.
 return bindings_.schema->MatchesAffected(env,affected.values,error);
}
bool Factory::CaptureInputs(JNIEnv*env,const std::vector<Definition>&definitions,std::string*error){
 if(!preflight_inputs_||preflight_inputs_!=&definitions||inputs_staged_||!Ready()||
    !native_source_stop::Session::InspectionAvailable()){
  if(error)*error="call-graph-windows-final-input-capture-order-refused";return false;
 }
 std::vector<native_schema_guard_verifier::Verifier::IncomingClass>affected;
 if(!bindings_.schema->CallGraphAffectedInputs(*run_.hierarchy.context,&affected,error))return false;
 std::vector<RootedInput>rooted;
 std::vector<std::unique_ptr<native_call_graph_incoming::Inventory>>incoming;
 rooted.reserve(affected.size());incoming.reserve(affected.size());
 for(const auto&entry:affected){
  struct LocalClass{JNIEnv*env;jobject value;~LocalClass(){if(value)env->DeleteLocalRef(value);}}local{env,roots_->LocalRef(env,entry.root)};
  if(!local.value||env->ExceptionCheck()){
   if(error)*error="call-graph-windows-affected-input-root-lost";return false;
  }
  const Definition*submitted=nullptr;size_t submitted_index=SIZE_MAX;
  for(size_t n=0;n<definitions.size();++n){const auto&definition=definitions[n];
   if(env->IsSameObject(local.value,definition.klass)){
    if(submitted){if(error)*error="call-graph-windows-affected-input-submission-ambiguous";return false;}
    submitted=&definition;submitted_index=n;
   }
  }
  if(env->ExceptionCheck())return false;
  std::vector<unsigned char>installed;
  if(!submitted&&!bindings_.installed->Read(*run_.hierarchy.context,static_cast<jclass>(local.value),entry.descriptor,&installed,error))return false;
  if(env->ExceptionCheck()||roots_->Generation()!=generation_||
     !runnable_jni::Bridge::BorrowOriginalScope(*run_.hierarchy.context)||!native_source_stop::Session::InspectionAvailable()){
   if(error)*error="call-graph-windows-independent-input-source-owner-lost";return false;
  }
  if(submitted){
   if(submitted_index>=submitted_incoming_.size()||!submitted_incoming_[submitted_index]||
      !submitted_incoming_[submitted_index]->Schema()||
      submitted_incoming_[submitted_index]->Schema()->descriptor!=entry.descriptor){
    if(error)*error="call-graph-windows-submitted-incoming-inventory-mismatch";return false;
   }
   rooted.push_back({entry.root,submitted_incoming_[submitted_index].get(),true});
  }else{
   auto inventory=std::make_unique<native_call_graph_incoming::Inventory>();
   if(!inventory->Capture(installed,entry.descriptor,error))return false;
   rooted.push_back({entry.root,inventory.get(),false});incoming.push_back(std::move(inventory));
  }
 }
 if(!MatchInputs(env,definitions,rooted,error))return false;
 inputs_=std::move(rooted);incoming_=std::move(incoming);inputs_staged_=true;return true;
}
bool Factory::Preflight(JNIEnv*env,jvmtiEnv*ti,const std::vector<Definition>&definitions,std::string*error){
 if(preflight_attempted_||!Ready()||!ti||!attached_||
    !native_source_stop::Session::InspectionAvailable()||
    !runnable_jni::Bridge::BorrowOriginalScope(*run_.hierarchy.context)){
  if(error)*error="call-graph-windows-public-preflight-order-refused";return false;
 }
 preflight_attempted_=true;
 // The native source observer already proved the ordered submission-Class
 // bijection. Capture the final inventories and affected census here, after
 // every agent callback. BEFORE execution preparation belongs to this same
 // preflight and cannot be a prerequisite for invoking it.
 preflight_inputs_=&definitions;
 struct CloseInputBorrow{const std::vector<Definition>*&slot;~CloseInputBorrow(){slot=nullptr;}}close{preflight_inputs_};
 if(!Stage(env,ti,definitions,error)||
    !CaptureInputs(env,definitions,error)||!PrepareBefore(env,ti,definitions,error)||!BeforePrepared()||!runnable_jni::Bridge::BorrowOriginalScope(*run_.hierarchy.context)||
    !native_source_stop::Session::InspectionAvailable()||
    !MatchInputs(env,definitions,inputs_,error)){
  phase_=Phase::Refused;return false;
 }
 // Copy final definitions and reserve weak identity bookkeeping before
 // original Run can mutate anything. A failed batch discards this candidate.
 record_=bindings_.installed->Records().Prepare(env,definitions);
 void*direct_self=nullptr;const void*direct_definitions=nullptr;
 if(!record_||!native_owned_removal::BorrowFinalInputBoundary(definitions,&direct_self,&direct_definitions)||
    direct_self!=run_.hierarchy.context->self||!record_->BindDirect(direct_self,direct_definitions)){
  phase_=Phase::Refused;if(error)*error="call-graph-windows-final-DEX-record-source-binding-refused";return false;
 }
 input_jvmti_=ti;preflight_complete_=true;return true;
}
bool Factory::PrepareBefore(JNIEnv*env,jvmtiEnv*ti,
 const std::vector<Definition>&definitions,std::string*error){
 if(!Ready()||!inputs_staged_||preflight_inputs_!=&definitions||
    run_.execution->LastReport().status!=native_call_graph_execution::Status::Empty){
  if(error)*error="call-graph-windows-before-preparation-order-refused";return false;
 }
 std::vector<jclass>classes;classes.reserve(definitions.size());
 for(const auto&definition:definitions)classes.push_back(definition.klass);
 if(!run_.hierarchy.pool->ReserveAnchor(error))return false;
 if(!run_.execution->Prepare(*run_.hierarchy.hooks,env,ti,classes,*run_.hierarchy.context,
    *bindings_.schema,*bindings_.source,*bindings_.payload.method_model,
    *bindings_.payload.commit.metadata,*bindings_.stack_declaration,error,
    bindings_.super_resolver))return false;
 if(!RetainArrayRoots(error))return false;
 // Resolution/calibration and the fresh public census can safepoint. No VMA
 // snapshot is retained until they have all finished. Inspection itself owns
 // the real stop and rechecks every carrier against the independently rooted
 // graph; the snapshot supplies bounded readability only.
 struct EndRead{Factory&factory;~EndRead(){factory.native_read_=false;factory.mappings_.reset();}}end{*this};
 if(!CaptureMappings(error))return false;
 const auto report=run_.execution->InspectActivation(run_.self_extent,read_);
 if(report.status!=native_call_graph_execution::Status::InspectedDrained){
  if(error)*error=report.failure?report.failure:"call-graph-windows-before-inspection-refused";return false;
 }
 return true;
}
bool Factory::CaptureMappings(std::string*error){
 native_read_=false;mappings_.reset();
 if(!run_.hierarchy.context||!runnable_jni::Bridge::BorrowOriginalScope(*run_.hierarchy.context)||
    !native_source_stop::Session::InspectionAvailable()||
    (!owner_.Running()&&!(preflight_inputs_&&phase_==Phase::Prepared&&preflight_attempted_&&!preflight_complete_))){
  if(error)*error="call-graph-windows-fresh-mapping-owner-refused";return false;
 }
 mappings_=std::make_unique<method_identity::ReadableMappingSnapshot>();
 if(!mappings_->Ready()){
  mappings_.reset();if(error)*error="call-graph-windows-fresh-mapping-capture-refused";return false;
 }
 native_read_=true;return true;
}
bool Factory::Extent(void*raw,const void*address,size_t*out)noexcept{
 auto&f=*static_cast<Factory*>(raw);
 // The ordinary snapshot is consumed only in the immediate native window.
 // Later readers require the real source stop; a transferred reservation by
 // itself cannot let Runnable code use cached metadata permissions.
 const bool stopped=f.owner_.Stop()&&f.owner_.Stop()->IsStopped(f.run_.hierarchy.context->self);
 return (f.native_read_||stopped)&&f.mappings_&&f.mappings_->Extent(address,out);
}
bool Factory::Decode(void*raw,jobject root,void**out)noexcept{
 auto&f=*static_cast<Factory*>(raw);const auto&r=f.bindings_.payload.reader;
 return f.native_read_&&r.decode&&r.decode(r.owner,root,out);
}
bool Factory::Readable(void*raw,const void*address,size_t bytes)noexcept{
 size_t extent=0;return address&&Extent(raw,address,&extent)&&extent>=bytes;
}
void*Factory::Reference(void*raw,void*object,size_t offset)noexcept{
 auto&f=*static_cast<Factory*>(raw);const auto&r=f.bindings_.payload.reader;
 if(offset>SIZE_MAX-4||!Readable(raw,object,offset+4)||!r.reference)return nullptr;
 return r.reference(r.owner,object,offset);
}
rooted_schema_plan::Reader Factory::Reader()noexcept{return {this,Decode,Readable,Reference};}
bool Factory::Definitions(const native_owned_removal::RootedGraph&graph,
 std::vector<native_call_graph_replacement::Definition>*out,std::string*error){
 const auto fail=[&](const char*why){if(error)*error=why;return false;};
 if(!out||!native_read_||!roots_||roots_->Generation()!=generation_)
  return fail("call-graph-windows-definition-root-generation-refused");
 std::vector<bool>seen(inputs_.size());
 std::vector<native_call_graph_replacement::Definition>definitions;
 definitions.reserve(inputs_.size());size_t previous=0;bool first=true;
 const auto reader=Reader();
 for(size_t p=0;p<graph.partitions.size();++p){const auto&part=graph.partitions[p];void*array=nullptr;
  if(!part.class_count||!part.old_classes||(!first&&part.holder_index<=previous)||
     part.class_count>size_t(std::numeric_limits<int32_t>::max())||part.class_count>(SIZE_MAX-12)/4||
     !reader.decode(reader.owner,part.old_classes,&array)||!reader.readable(reader.owner,array,12+4*part.class_count))
   return fail("call-graph-windows-original-Holder-class-array-refused");
  int32_t length=0;std::memcpy(&length,static_cast<const char*>(array)+8,4);
  if(length<0||size_t(length)!=part.class_count)return fail("call-graph-windows-original-Holder-class-count-refused");
  first=false;previous=part.holder_index;
  for(size_t n=0;n<part.class_count;++n){void*klass=reader.reference(reader.owner,array,12+4*n);size_t found=SIZE_MAX;
   if(!klass)return fail("call-graph-windows-original-Holder-class-root-lost");
   for(size_t i=0;i<inputs_.size();++i){void*original=nullptr;
    if(!roots_->Read(inputs_[i].original,&original))return fail("call-graph-windows-independent-input-root-lost");
    if(original==klass){if(found!=SIZE_MAX)return fail("call-graph-windows-duplicate-independent-input");found=i;}
   }
   if(found==SIZE_MAX||seen[found]||!inputs_[found].incoming||!inputs_[found].incoming->Schema())
    return fail("call-graph-windows-Holder-missing-independent-incoming-DEX");
   seen[found]=true;definitions.push_back({p,n,inputs_[found].incoming});
  }
 }
 for(bool present:seen)if(!present)return fail("call-graph-windows-Holder-omitted-affected-input");
 *out=std::move(definitions);return true;
}
bool Factory::Window(JNIEnv*env,jvmtiEnv*ti,const std::vector<jclass>&classes,
 const native_owned_removal::RootedGraph&graph,native_owned_removal::GraphWindow window,std::string*error){
 // Allocation failure must close the ordinary read episode before the source
 // request dispatches refund. Retained VMA facts remain stopped-only.
 struct EndRead{bool&active;~EndRead(){active=false;}}end_read{native_read_};
 const auto fail=[&](const char*why){native_read_=false;phase_=Phase::Refused;if(error)*error=why;return false;};
 if((window!=native_owned_removal::GraphWindow::Verified&&window!=native_owned_removal::GraphWindow::Collected)||
    env!=run_.hierarchy.context->env||!preflight_complete_||ti!=input_jvmti_||!owner_.Running()||!owner_.Driver()||!owner_.Loading()||
    !graph.holder||!graph.metadata.count||roots_->Generation()!=generation_||
    !runnable_jni::Bridge::BorrowOriginalScope(*run_.hierarchy.context))
  return fail("call-graph-windows-original-callback-owner-refused");
 if(window==native_owned_removal::GraphWindow::Verified){
  if(phase_!=Phase::Prepared||!CaptureMappings(error))return fail("call-graph-windows-Verified-order-or-mappings-refused");
  std::vector<native_call_graph_replacement::Definition>definitions;
  if(!Definitions(graph,&definitions,error)){native_read_=false;phase_=Phase::Refused;return false;}
  // Definitions retain ordinals/inventories only. Drop the decoded snapshot
  // before Stage/resolution/census performs its real JNI and safepoints.
  native_read_=false;mappings_.reset();
  if(!run_.execution->PrepareReplacements(*run_.hierarchy.hooks,*run_.hierarchy.context,ti,
      *owner_.Loading(),owner_.Driver(),graph,definitions,*roots_,error)||
     !bindings_.schema->CheckWindow(env,ti,classes,native_schema_guard_verifier::Window::Verified,error)||
     !RetainArrayRoots(error)||!CaptureMappings(error)){native_read_=false;phase_=Phase::Refused;return false;}
  const auto report=run_.execution->InspectActivation(run_.self_extent,read_);
  native_read_=false;mappings_.reset();
  if(report.status!=native_call_graph_execution::Status::ReplacementInspectedDrained||!report.union_complete)
   return fail(report.failure?report.failure:"call-graph-windows-complete-replacement-inspection-refused");
  holder_=graph.holder;holder_count_=graph.metadata.count;phase_=Phase::Verified;return true;
 }
 if(window!=native_owned_removal::GraphWindow::Collected||phase_!=Phase::Verified||holder_!=graph.holder||holder_count_!=graph.metadata.count)
  return fail("call-graph-windows-Collected-original-Holder-order-refused");
 // Public checks may safepoint: no previous mapping cohort survives them.
 native_read_=false;mappings_.reset();
 if(!bindings_.schema->CheckWindow(env,ti,classes,native_schema_guard_verifier::Window::Collected,error)||
    !CaptureMappings(error)){phase_=Phase::Refused;return false;}
 if(!BindRootRegistry(error)){phase_=Phase::Refused;mappings_.reset();return false;}
 auto payload_bindings=bindings_.payload;payload_bindings.commit.read=&read_;payload_bindings.reader=Reader();
 const bool prepared=payload_.Prepare(*run_.hierarchy.context,graph,owner_,payload_bindings,registry_,error);
 native_read_=false;
 if(!prepared){phase_=Phase::Refused;mappings_.reset();return false;}
 // All native operands are now independently registered. Keep bounded VMA
 // facts for actual stopped readers through the continuous source episode;
 // they confer no ordinary pointer borrow after this callback returns.
 phase_=Phase::Collected;return true;
}
bool Factory::BindRootRegistry(std::string*error){
 if(phase_!=Phase::Verified||!native_read_||!owner_.Running()||!owner_.Loading()->Paused()||
    registry_.Size()||!prepared_root_slots_.empty()||
    !run_.hierarchy.pool->ReadyForSourceAccess()||
    !registry_.BindNative(this,InsertPreparedRoot,ReadPreparedRoot,ClearPreparedRoots,RootInsertionEpoch)){
  if(error)*error="call-graph-windows-native-root-registry-binding-refused";return false;
 }
 return true;
}
uint64_t Factory::RootInsertionEpoch(void*raw)noexcept{
 const auto&f=*static_cast<Factory*>(raw);
 return f.phase_==Phase::Verified&&f.native_read_&&f.owner_.Running()&&
  f.owner_.Loading()->Paused()&&f.roots_->Generation()==f.generation_&&
  runnable_jni::Bridge::BorrowOriginalScope(*f.run_.hierarchy.context)&&
  native_source_stop::Session::InspectionAvailable()&&f.run_.hierarchy.pool->ReadyForSourceAccess()?
  f.generation_:0;
}
bool Factory::InsertPreparedRoot(void*raw,void*managed,size_t*out){
 auto&f=*static_cast<Factory*>(raw);
 if(!out||!managed||uintptr_t(managed)>UINT32_MAX||!RootInsertionEpoch(raw))return false;
 // Reserve before linking the source slot, so allocation cannot orphan a
 // successfully installed root outside this registry's cleanup cohort.
 if(f.prepared_root_slots_.size()==f.prepared_root_slots_.max_size())return false;
 if(f.prepared_root_slots_.size()==f.prepared_root_slots_.capacity()){
  const size_t capacity=f.prepared_root_slots_.capacity(),limit=f.prepared_root_slots_.max_size();
  const size_t next=capacity?capacity+(capacity>limit-capacity?limit-capacity:capacity):std::min(size_t(64),limit);
  f.prepared_root_slots_.reserve(next);
 }
 auto*slot=f.run_.hierarchy.pool->Add(managed,nullptr);if(!slot)return false;
 *out=f.prepared_root_slots_.size();f.prepared_root_slots_.push_back(slot);return true;
}
bool Factory::RetainArrayRoots(std::string*error){
 if(roots_->RetainArrays({this,RetainArray,ReadArray,ReleaseArray,ArrayStaging}))return true;
 if(error)*error="call-graph-array-native-retention-refused";return false;
}
bool Factory::ArrayStaging(void*raw)noexcept{
 auto&f=*static_cast<Factory*>(raw);
 return f.run_.hierarchy.context&&runnable_jni::Bridge::BorrowOriginalScope(*f.run_.hierarchy.context)&&
  native_source_stop::Session::InspectionAvailable()&&f.run_.hierarchy.pool&&f.run_.hierarchy.pool->ReadyForSourceAccess();
}
void*Factory::RetainArray(void*raw,void*array){
 auto&f=*static_cast<Factory*>(raw);
 return ArrayStaging(raw)&&array&&uintptr_t(array)<=UINT32_MAX?f.run_.hierarchy.pool->Add(array,nullptr):nullptr;
}
bool Factory::ReadArray(void*raw,const void*token,void**out)noexcept{
 auto&f=*static_cast<Factory*>(raw);
 // Test the actual stop first. The ordinary Runnable service may consult
 // mappings on a thread different from its calibration-time owner; no such
 // OS query or allocation belongs in this native stopped reader.
 if(f.FinalRootWindow())return f.ReadStoppedRoot(token,out);
 return f.run_.hierarchy.pool->Read(token,out);
}
void Factory::ReleaseArray(void*raw,void*token)noexcept{
 auto&f=*static_cast<Factory*>(raw);
 if(!ArrayStaging(raw)||!f.run_.hierarchy.pool->Write(token,nullptr))std::abort();
}
bool Factory::FinalRootWindow()const noexcept{
 // A refund supplies its own fresh mapping context. Physical HandleScope
 // cells need only the exact operation's actual live stop, not an older VMA
 // snapshot from the preparation callback.
 if(run_.execution&&run_.execution->InspectionStopped())return true;
 if(!mappings_||!mappings_->Ready())return false;
 return owner_.Running()&&owner_.Stop()&&owner_.Stop()->IsStopped(run_.hierarchy.context->self)&&
  owner_.Loading()&&native_source_class_loading::Run::Current()==owner_.Loading()&&
  owner_.Loading()->Paused()&&mappings_&&mappings_->Ready();
}
bool Factory::Verify(const source_commit::Plan&plan,std::string*error)const{
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 const auto*policy=bindings_.payload.commit.interpretation;
 if(&plan!=&payload_.plan_||!payload_.attempted_||!holder_||!holder_count_||
    !owner_.Running()||!owner_.Loading()||
    !run_.execution->Replacements().OwnsSource(*owner_.Loading(),owner_.Driver(),holder_)||
    !policy||!run_.execution->InterpretationPolicyReady(*policy))
  return deny("call-graph-access-permissions-complete-source-union-or-installed-policy-unproved");
 if(FinalRootWindow()){
  if(!run_.execution->InterpretationPrepared(read_))
   return deny("call-graph-access-permissions-stopped-publication-reservation-unproved");
  return true;
 }
 // The first ordinary check prepares the verifier's access-shortcut slots.
 // All rejectable allocation/publication work still follows before the stop;
 // readiness here cannot substitute for the stopped check above.
 if(phase_!=Phase::Verified||!native_read_||!mappings_||!mappings_->Ready()||
    !runnable_jni::Bridge::BorrowOriginalScope(*run_.hierarchy.context)||
    !native_source_stop::Session::InspectionAvailable()||!owner_.Loading()->Paused()||
    !run_.execution->Replacements().ValidateOrdinary(read_))
  return deny("call-graph-access-permissions-original-collected-preparation-window-refused");
 return true;
}
void Factory::BeginCommit(const source_commit::Plan&plan)const{
 if(!owner_.Stop()||!owner_.Stop()->IsFinalWindow(run_.hierarchy.context->self)||
    !FinalRootWindow()||!Verify(plan,nullptr))std::abort();
 // The verifier owns the already frozen shortcut locations. This callback
 // consumes execution permission only; it adds no flags, hooks or entries.
}
bool Factory::ReadStoppedRoot(const void*slot,void**out)const noexcept{
 return FinalRootWindow()&&run_.hierarchy.pool->ReadStopped(*this,slot,out);
}
bool Factory::ReadPreparedRoot(void*raw,size_t index,void**out)noexcept{
 const auto&f=*static_cast<Factory*>(raw);
 return index<f.prepared_root_slots_.size()&&f.ReadStoppedRoot(f.prepared_root_slots_[index],out);
}
void Factory::ClearPreparedRoots(void*raw)noexcept{
 auto&f=*static_cast<Factory*>(raw);
 if(f.prepared_root_slots_.empty())return;
 if(!runnable_jni::Bridge::BorrowOriginalScope(*f.run_.hierarchy.context)||
    !native_source_stop::Session::InspectionAvailable()||!f.run_.hierarchy.pool->ReadyForSourceAccess())std::abort();
 for(auto*slot:f.prepared_root_slots_)if(!f.run_.hierarchy.pool->Write(slot,nullptr))std::abort();
 f.prepared_root_slots_.clear();
}
bool Factory::Attach(native_owned_removal::Provider*out,std::string*error){
 if(attached_||!out||!Ready()){
  if(error)*error="call-graph-windows-transaction-provider-order-refused";return false;
 }
 native_owned_removal::Provider preparation{};preparation.state=this;
 preparation.metadata=run_.hierarchy.metadata;preparation.allow_field_removal=true;preparation.schema_evolution=true;
 preparation.ready=[](void*raw){return static_cast<Factory*>(raw)->Ready();};
 preparation.preflight=[](JNIEnv*env,jvmtiEnv*ti,const std::vector<Definition>&definitions,void*raw,std::string*reason){
  auto&f=*static_cast<Factory*>(raw);
  return f.Preflight(env,ti,definitions,reason);
 };
 preparation.closed_graph=[](JNIEnv*env,jvmtiEnv*ti,const std::vector<jclass>&classes,
  const native_owned_removal::RootedGraph&graph,native_owned_removal::GraphWindow window,void*raw,std::string*reason){
  return static_cast<Factory*>(raw)->Window(env,ti,classes,graph,window,reason);
 };
 preparation.record_success=[](void*raw)noexcept{
  auto&f=*static_cast<Factory*>(raw);
  if(f.recorded_||!f.record_||!f.preflight_complete_||f.phase_!=Phase::Collected||f.owner_.SourceViewsLive())std::abort();
  f.record_->Commit();f.recorded_=true;
 };
 if(!owner_.BindPrivateRootRelease(this,ReleasePrivateRoots)||
    !owner_.AttachProvider(preparation,out,error))return false;
 attached_=true;return true;
}
bool Factory::ReleasePrivateRoots(void*raw,const char**refusal)noexcept{
 auto&f=*static_cast<Factory*>(raw);
 if(refusal)*refusal=nullptr;
 const auto deny=[&](const char*why){if(refusal)*refusal=why;return false;};
 if(f.roots_released_||!f.roots_||f.roots_->Generation()!=f.generation_||
    !f.owner_.SourceViewsLive()||!runnable_jni::Bridge::BorrowOriginalScope(*f.run_.hierarchy.context)||
    !native_source_stop::Session::InspectionAvailable()||f.run_.execution->HasOwnedEntries())
  return deny("call-graph-windows-private-root-release-owner-refused");
 f.native_read_=false;f.phase_=Phase::Refused;f.mappings_.reset();
 // The shared Store belongs to this single operation. Entry journals and all
 // private record checks are already closed; no graph reader can run again.
 // Release actual JNI array globals before original source DEX cleanup.
 f.roots_->Clear(f.run_.hierarchy.context->env);f.roots_released_=true;
 return true;
}
}
