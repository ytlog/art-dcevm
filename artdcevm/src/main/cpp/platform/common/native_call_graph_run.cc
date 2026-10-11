#include "platform/common/native_call_graph_run.h"
#include <cstdlib>

namespace artdcevm::native_call_graph_run {
Owner::~Owner(){
 if(phase_==Phase::Empty||phase_==Phase::Beginning)CloseBeforeRun();
 if(phase_!=Phase::Closed)std::abort();
}
bool Owner::Running()const noexcept{
 return (phase_==Phase::Running||phase_==Phase::Continued||phase_==Phase::Finalized||phase_==Phase::Committed)&&
  loading_&&loading_->OwnsDriver(driver_)&&hierarchy_&&hierarchy_->OwnsDriver(driver_)&&
  bindings_.hierarchy.context&&bindings_.compilation&&bindings_.compilation->Validate(compilation_receipt_,bindings_.hierarchy.context->self);
}
bool Owner::FailBegin(std::string*error,const char*reason)noexcept{
 try{if(error)*error=reason;}catch(...){}
 CloseBeforeRun();return false;
}
bool Owner::Begin(void*driver,std::string*error){
 if(phase_!=Phase::Empty){if(error)*error="call-graph-run-repeated-begin";return false;}
 phase_=Phase::Beginning;driver_=driver;
 const auto&h=bindings_.hierarchy;
 if(!h.context)return FailBegin(error,"call-graph-run-original-callback-context-missing");
 const auto&context=*h.context;
 if(!driver||!provider_attached_||!preflight_complete_||!preflight_jvmti_||
    !bindings_.loading||!bindings_.stop||!bindings_.stop->Calibrated()||
    !bindings_.stop->DexPublicationCalibrated()||!bindings_.dex_registry||!bindings_.dex_registry->Calibrated()||
    !bindings_.execution||!bindings_.compilation||!bindings_.self_extent||!context.env||!context.vm||!context.self||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||context.env->ExceptionCheck()||
    !native_source_stop::Session::InspectionAvailable()||native_source_class_loading::Run::Current()||
    native_hierarchy_session::Session::Current()||!h.hooks||!h.metadata||!h.metadata->Calibrated()||
    !h.roots||!h.roots->Calibrated()||!h.pool||!h.pool->UsesService(*h.roots)||!h.pool->ReadyForSourceAccess()||
    !h.authority||!h.authority->Staged()||!h.allocation||!h.allocation->Calibrated()||!h.access||!h.refuse||
    !native_hierarchy_source_checks::Ready(h.failure_observer)||h.ledger||(h.driver&&h.driver!=driver)||
    h.refund_owner||h.before_private_discard||h.refund_complete||h.private_roots_owner||h.release_private_roots||
    !private_roots_owner_||!release_private_roots_||
    bindings_.execution->LastReport().status!=native_call_graph_execution::Status::InspectedDrained)
  return FailBegin(error,"call-graph-run-original-prepared-operation-unavailable");
 const char*refusal=nullptr;
 if(!bindings_.compilation->Capture(context,&compilation_receipt_,&refusal))
  return FailBegin(error,refusal?refusal:"call-graph-run-original-compilation-boundary-unobserved");
 try{
  stop_=std::make_unique<native_source_stop::Session>(*bindings_.stop);
  loading_=std::make_unique<native_source_class_loading::Run>(*bindings_.loading,driver,context);
  if(!loading_->OwnsDriver(driver))return FailBegin(error,"call-graph-run-original-loading-owner-refused");
  commit_=std::make_unique<native_call_graph_commit::SourceOwner>(context,*loading_,driver,*stop_,
    *bindings_.compilation,compilation_receipt_);
  ledger_=std::make_unique<native_hierarchy_private_ledger::Ledger>(driver,context,*h.pool,*h.access,
    *h.metadata,*bindings_.dex_registry,commit_.get());
  ledger_active_=ledger_->OwnsDriver(driver);
  if(!ledger_active_)return FailBegin(error,"call-graph-run-private-ledger-owner-refused");
  refund_=std::make_unique<native_call_graph_execution::SourceRefund>(*bindings_.execution,*loading_,driver);
  auto hierarchy_bindings=h;hierarchy_bindings.driver=driver;hierarchy_bindings.ledger=ledger_.get();
  if(!refund_->Bind(hierarchy_bindings))return FailBegin(error,"call-graph-run-execution-refund-binding-refused");
  hierarchy_bindings.private_roots_owner=this;hierarchy_bindings.release_private_roots=ReleasePrivateRoots;
  hierarchy_=std::make_unique<native_hierarchy_session::Session>(hierarchy_bindings);
  if(!hierarchy_->Ready())return FailBegin(error,"call-graph-run-hierarchy-owner-refused");
  hierarchy_active_=true;phase_=Phase::Running;return true;
 }catch(...){CloseBeforeRun();throw;}
}
bool Owner::BeforeVerification(void*owner,const transaction_root::Iterator*iterator,std::string*error){
 if(phase_!=Phase::Running||!Running()){
  if(error)*error="call-graph-run-verification-owner-unavailable";return false;
 }
 return hierarchy_->BeforeVerification(owner,iterator,error);
}
void Owner::AfterVerification(void*owner,const transaction_root::Iterator*iterator,bool result)noexcept{
 if(!hierarchy_||!hierarchy_active_||!loading_||!loading_->OwnsDriver(driver_))std::abort();
 hierarchy_->AfterVerification(owner,iterator,result);
}
bool Owner::ContinueCollected(const char**refusal)noexcept{
 if(refusal)*refusal=nullptr;
 if(phase_!=Phase::Running||!Running()||!commit_||!commit_->PreparedService()){
  if(refusal)*refusal="call-graph-run-collected-service-unprepared";return false;
 }
 native_hierarchy_session::Session::RefundDeferral deferral(*hierarchy_);
 if(!deferral.Ready()){
  if(refusal)*refusal="call-graph-run-collected-refund-deferral-unavailable";return false;
 }
 if(!commit_->ContinueCollected(*bindings_.hierarchy.context,*hierarchy_,bindings_.self_extent,refusal))return false;
 if(phase_!=Phase::Running)std::abort();
 phase_=Phase::Continued;return true;
}
std::unique_ptr<source_commit::Services>Owner::Finalize(const void*holder,const source_commit::Plan**plan)noexcept{
 if(phase_!=Phase::Continued||!Running()||!commit_||!stop_->IsFinalWindow(bindings_.hierarchy.context->self))std::abort();
 auto service=commit_->TakeFinalService(holder,plan);
 if(!service||!plan||!*plan)std::abort();phase_=Phase::Finalized;return service;
}
void Owner::Commit(const source_commit::Plan&plan)noexcept{
 if(phase_!=Phase::Finalized||!Running()||!commit_)std::abort();
 commit_->Commit(plan);phase_=Phase::Committed;
}
void Owner::Refund()noexcept{
 if(phase_==Phase::Closed||phase_==Phase::Refunded)return;
 if(phase_==Phase::Empty||phase_==Phase::Beginning){CloseBeforeRun();return;}
 // Once Collected transfers the reservation, failure cannot be turned into
 // an ordinary source refund. All rejectable work must precede continuation.
 if(phase_!=Phase::Running||!hierarchy_||!hierarchy_active_||!loading_||!loading_->OwnsDriver(driver_))std::abort();
 // The hierarchy's actual Finish/verification/Collected deferrals retain the
 // private storage until native views and the rejectable stop have unwound.
 hierarchy_->Refund();phase_=Phase::Refunded;
}
void Owner::End(void*driver,bool success)noexcept{
 if(phase_==Phase::Closed){if(success||(driver_&&driver_!=driver))std::abort();driver_=nullptr;return;}
 if(driver!=driver_||!loading_||!loading_->OwnsDriver(driver_)||!hierarchy_||!hierarchy_active_||
    (success?phase_!=Phase::Committed:phase_!=Phase::Refunded))std::abort();
 if(!hierarchy_->ReleaseAfterRun(success))std::abort();hierarchy_active_=false;ledger_active_=false;
 if(success)commit_->ReleaseAfterSuccess(driver);
 else commit_->ReleaseAfterFailure(driver,*hierarchy_);
 if(success){if(!loading_->Complete(true))std::abort();}
 else loading_->Complete(false);
 DestroyViews();driver_=nullptr;phase_=Phase::Closed;
}
void Owner::CloseBeforeRun()noexcept{
 if(phase_==Phase::Closed)return;
 if((phase_!=Phase::Empty&&phase_!=Phase::Beginning)||hierarchy_active_||
    (stop_&&(stop_->Reserved()||stop_->IsStopped(bindings_.hierarchy.context->self))))std::abort();
 // No original Finish or other source body was entered. Consequently the
 // ledger must contain no private ownership; this is not a disposal shortcut.
 hierarchy_.reset();refund_.reset();
 if(ledger_active_){if(!ledger_||!ledger_->CloseBeforeRun())std::abort();ledger_active_=false;}
 ledger_.reset();commit_.reset();
 if(loading_&&loading_->OwnsDriver(driver_))loading_->Complete(false);
 loading_.reset();stop_.reset();
 if(bindings_.execution&&bindings_.execution->HasOwnedEntries()){
  const auto report=bindings_.execution->RefundBeforeRun();
  if(report.failure||!report.owned_entries_removed||bindings_.execution->HasOwnedEntries())std::abort();
 }
 phase_=Phase::Closed;
}
void Owner::DestroyViews()noexcept{
 hierarchy_.reset();refund_.reset();ledger_.reset();commit_.reset();loading_.reset();stop_.reset();
}
bool Owner::BindPrivateRootRelease(void*owner,bool(*release)(void*,const char**)noexcept)noexcept{
 if(phase_!=Phase::Empty||private_roots_owner_||release_private_roots_||!owner||!release)return false;
 private_roots_owner_=owner;release_private_roots_=release;return true;
}
bool Owner::ReleasePrivateRoots(void*raw,const char**refusal)noexcept{
 if(refusal)*refusal=nullptr;
 auto&o=*static_cast<Owner*>(raw);
 const auto deny=[&](const char*why){if(refusal)*refusal=why;return false;};
 if((o.phase_!=Phase::Running&&o.phase_!=Phase::Refunded)||!o.hierarchy_active_||
    !o.loading_||!o.loading_->OwnsDriver(o.driver_)||!o.commit_||!o.stop_||
    o.stop_->Reserved()||o.stop_->IsStopped(o.bindings_.hierarchy.context->self)||
    !o.bindings_.execution||o.bindings_.execution->HasOwnedEntries()||
    !o.private_roots_owner_||!o.release_private_roots_)
  return deny("call-graph-run-private-root-release-phase-or-entry-refused");
 if(!o.commit_->DropPreparedReferences(o.driver_))
  return deny("call-graph-run-prepared-root-release-refused");
 return o.release_private_roots_(o.private_roots_owner_,refusal);
}
bool Owner::AttachProvider(const native_owned_removal::Provider&factory,
 native_owned_removal::Provider*out,std::string*error){
 if(!out||!bindings_.hierarchy.context||provider_attached_||phase_!=Phase::Empty||!factory.ready||!factory.preflight||
    !factory.closed_graph||!factory.schema_evolution||factory.metadata!=bindings_.hierarchy.metadata||
    factory.begin_run||factory.end_run||factory.before_verification||factory.after_verification||
    factory.refund||factory.commit||factory.finalize){
  if(error)*error="call-graph-run-preparation-provider-or-lifecycle-conflict";return false;
 }
 preparation_=factory;provider_attached_=true;
 native_owned_removal::Provider provider{};provider.state=this;
 provider.metadata=factory.metadata;provider.allow_field_removal=factory.allow_field_removal;provider.schema_evolution=true;
 provider.ready=[](void*raw){auto&o=*static_cast<Owner*>(raw);
  return o.phase_==Phase::Empty&&o.provider_attached_&&o.preparation_.ready(o.preparation_.state);
 };
 provider.prepare=[](const source_commit::Plan&,void*,std::string*reason)->std::unique_ptr<source_commit::Services>{
  if(reason)*reason="call-graph-run-service-requires-original-final-boundary";return nullptr;
 };
 provider.preflight=[](JNIEnv*env,jvmtiEnv*ti,const std::vector<Definition>&definitions,void*raw,std::string*reason){
  auto&o=*static_cast<Owner*>(raw);
  if(o.phase_!=Phase::Empty||o.preflight_attempted_||!ti||env!=o.bindings_.hierarchy.context->env){
   if(reason)*reason="call-graph-run-preflight-owner-or-order-refused";return false;
  }
  o.preflight_attempted_=true;
  if(!o.preparation_.preflight(env,ti,definitions,o.preparation_.state,reason))return false;
  if(o.phase_!=Phase::Empty||env->ExceptionCheck()||!runnable_jni::Bridge::BorrowOriginalScope(*o.bindings_.hierarchy.context)||
     !native_source_stop::Session::InspectionAvailable()){
   if(reason)*reason="call-graph-run-preflight-source-context-lost";return false;
  }
  o.preflight_jvmti_=ti;o.preflight_complete_=true;return true;
 };
 provider.begin_run=[](void*driver,void*raw,std::string*reason){return static_cast<Owner*>(raw)->Begin(driver,reason);};
 provider.end_run=[](void*driver,bool success,void*raw)noexcept{static_cast<Owner*>(raw)->End(driver,success);};
 if(factory.record_success)provider.record_success=[](void*raw)noexcept{
  auto&o=*static_cast<Owner*>(raw);
  if(o.phase_!=Phase::Closed||!o.preparation_.record_success)std::abort();
  o.preparation_.record_success(o.preparation_.state);
 };
 provider.before_verification=[](void*owner,const transaction_root::Iterator*iterator,void*raw,std::string*reason){
  return static_cast<Owner*>(raw)->BeforeVerification(owner,iterator,reason);
 };
 provider.after_verification=[](void*owner,const transaction_root::Iterator*iterator,bool result,void*raw)noexcept{
  static_cast<Owner*>(raw)->AfterVerification(owner,iterator,result);
 };
 provider.refund=[](void*raw)noexcept{static_cast<Owner*>(raw)->Refund();};
 provider.commit=[](const source_commit::Plan&plan,void*raw)noexcept{static_cast<Owner*>(raw)->Commit(plan);};
 provider.finalize=[](const void*holder,const source_commit::Plan**plan,void*raw)noexcept{
  return static_cast<Owner*>(raw)->Finalize(holder,plan);
 };
 provider.closed_graph=[](JNIEnv*env,jvmtiEnv*ti,const std::vector<jclass>&classes,
  const native_owned_removal::RootedGraph&graph,native_owned_removal::GraphWindow window,void*raw,std::string*reason){
  auto&o=*static_cast<Owner*>(raw);
  if((window!=native_owned_removal::GraphWindow::Verified&&window!=native_owned_removal::GraphWindow::Collected)||
     o.phase_!=Phase::Running||!o.Running()||!o.preflight_complete_||ti!=o.preflight_jvmti_||env!=o.bindings_.hierarchy.context->env||
     !o.preparation_.closed_graph(env,ti,classes,graph,window,o.preparation_.state,reason))return false;
  if(o.phase_!=Phase::Running||!o.Running()||!o.bindings_.execution->Replacements().OwnsSource(*o.loading_,o.driver_,graph.holder))return false;
  if(window==native_owned_removal::GraphWindow::Verified){
   const auto&report=o.bindings_.execution->LastReport();
   return report.status==native_call_graph_execution::Status::ReplacementInspectedDrained&&report.union_complete;
  }
  if(window!=native_owned_removal::GraphWindow::Collected)return false;
  const char*refusal=nullptr;
  if(o.ContinueCollected(&refusal))return true;
  if(reason)*reason=refusal?refusal:"call-graph-run-collected-continuation-refused";return false;
 };
 *out=provider;return true;
}
}
