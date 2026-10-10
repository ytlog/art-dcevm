#include "platform/common/native_schema_run_state.h"
#include "platform/common/rooted_schema_plan.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/redefinition_gate.h"
#include "platform/abi/arm64/common/redefiner_prefix.h"
#include <cstring>
#include <cstdlib>
#include <new>
namespace artdcevm::native_schema_run_state {
namespace {thread_local RunState* current=nullptr;bool Fail(std::string*e,const char*s){if(e)*e=s;return false;}void Require(bool v){if(!v)std::abort();}}
struct RunState::State {
 Dependencies d;modern_field_metadata::ReadContext read_context{};void* driver;const runnable_jni::Context context;
 source_handle_roots::Pool pool;native_hierarchy_source_cache::AllocatorBatchAccess access;
 native_hierarchy_graph::Authority authority;native_hierarchy_receiver::Preservation receivers;
 native_boot_method_catalog::Rooted boot_catalog;
 std::unique_ptr<native_boot_aot_unaffected::Source> boot_unaffected;
 whole_process_frames::Service::Prepared frame_storage;
 std::unique_ptr<native_source_stop::Session> stop;
 std::unique_ptr<native_required_cleanup::Service> cleanup;
 std::unique_ptr<native_hierarchy_private_ledger::Ledger> ledger;
 std::unique_ptr<native_hierarchy_source_cache::Mapper> mapper;
 native_hierarchy_cache::Owner incoming;
 std::unique_ptr<native_hierarchy_source_cache::Composite> caches;
 std::unique_ptr<native_source_class_loading::Run> loading;
 std::unique_ptr<native_hierarchy_session::Session> hierarchy;
 std::unique_ptr<native_required_cleanup::Facade> facade;
 prepared_roots::Bindings plan_roots;
 source_commit::Plan* plan=nullptr;const void* holder=nullptr;
 size_t extent=0;RunState* previous=nullptr;
 enum class Phase {Constructed,Running,Collected,Finalized,Committed,Refunded,Ended};
 Phase phase=Phase::Constructed;
 struct RootReader final:source_handle_roots::StoppedReader {
  const State& state;explicit RootReader(const State&s):state(s){}
  bool FinalRootWindow()const noexcept override{return state.cleanup&&state.holder&&state.cleanup->IsStopped(state.driver,static_cast<const transaction_root::Holder*>(state.holder));}
  bool ReadStoppedRoot(const void* token,void** value)const noexcept override{return state.pool.ReadStopped(*this,token,value);}
 };
 static uint64_t RootEpoch(void* raw)noexcept{
  const auto& s=*static_cast<State*>(raw);
  // The actual collected source callback has frozen loading and Holder identity;
  // no JNI or safepoint occurs during native registration in this one window.
  return s.phase==Phase::Running&&s.plan&&s.holder&&s.loading&&s.loading->Paused()&&s.pool.ReadyForSourceAccess()?1:0;
 }
 static bool RootInsert(void* raw,void* managed,size_t* slot){
  auto& s=*static_cast<State*>(raw);if(!slot||!RootEpoch(raw))return false;
  auto* token=s.pool.Add(managed,nullptr);if(!token)return false;*slot=uintptr_t(token);return true;
 }
 static bool RootRead(void* raw,size_t slot,void** value){
  auto& s=*static_cast<State*>(raw);if(!slot||slot==SIZE_MAX)return false;
  RootReader stopped(s);
  return stopped.FinalRootWindow()?stopped.ReadStoppedRoot(reinterpret_cast<const void*>(slot),value):
      RootEpoch(raw)&&s.pool.Read(reinterpret_cast<const void*>(slot),value);
 }
 static void RootClear(void*){}
 State(const Dependencies& deps,void* dr,const runnable_jni::Context&ctx)
  :d(deps),driver(dr),context(ctx),pool(*deps.roots),access(pool),receivers(*deps.members.metadata){}
};
RunState::RunState(const Dependencies&d,void*driver,const runnable_jni::Context&c):state_(new State(d,driver,c)){}
std::unique_ptr<RunState> RunState::Begin(const Dependencies&d,void*driver,const runnable_jni::Context&c,jvmtiEnv*ti,
 const std::vector<native_hierarchy_graph::Input>&inputs,const CacheCalibration&w,std::string*error){
 if(current||native_source_class_loading::Run::Current()||native_hierarchy_session::Session::Current()||!driver||!c.env||!c.self||!ti||!d.hooks||!d.loading||!d.stop||!d.stop->Calibrated()||!d.roots||!d.allocator||!d.dex_registry||!d.dex_registry->Calibrated()||!d.lookup||!d.lookup->Calibrated()||!d.frames||!d.cleanup||!d.provider||!d.registry||!d.members.metadata||!d.members.fields||!d.members.cache||!d.members.runtime.runtime||!native_hierarchy_source_checks::Ready(native_owned_removal::ObserveSourceFailure)||!d.mapping.owner||!d.mapping.refresh_stopped||!d.mapping.extent){
  Fail(error,"Full schema Run requires independent calibrated actual owners and continuous source stop");return nullptr;
 }
 size_t extent=0;if(!method_identity::ReadableMappingExtent(driver,&extent)||extent<sizeof(abi::arm64::android_common::RedefinerPrefix)){Fail(error,"Original schema driver is not mapped");return nullptr;}
 abi::arm64::android_common::RedefinerPrefix p;std::memcpy(&p,driver,sizeof(p));
 if(p.self!=c.self||p.runtime!=d.members.runtime.runtime||!p.environment||p.type!=0||p.result!=JVMTI_ERROR_INTERNAL||c.vm!=d.members.runtime.receivers.java_vm||!redefinition_gate::Owned(static_cast<JavaVM*>(d.members.runtime.receivers.java_vm))){Fail(error,"Original schema driver differs from runtime/self/direct owner");return nullptr;}
 auto result=std::unique_ptr<RunState>(new RunState(d,driver,c));
 if(!result->Start(ti,inputs,w,error))return nullptr;
 return result;
}
bool RunState::Start(jvmtiEnv*ti,const std::vector<native_hierarchy_graph::Input>&inputs,const CacheCalibration&w,std::string*error){
 auto&s=*state_;s.previous=current;current=this;
 if(!method_identity::ReadableMappingExtent(s.context.self,&s.extent)||!s.pool.ReadyForSourceAccess()||!s.d.frames->Prepare(&s.frame_storage))return Fail(error,"Source scanner/native root storage preparation failed");
 s.stop=std::make_unique<native_source_stop::Session>(*s.d.stop);
 s.cleanup=std::make_unique<native_required_cleanup::Service>(*s.d.cleanup,s.context.self,s.driver,*s.stop,*s.d.frames,s.frame_storage,*s.d.members.cache,s.d.members.runtime.receivers.jit_code_cache,*s.d.provider,*s.d.registry);
 s.loading=std::make_unique<native_source_class_loading::Run>(*s.d.loading,s.driver,s.context);
 s.ledger=std::make_unique<native_hierarchy_private_ledger::Ledger>(s.driver,s.context,s.pool,s.access,*s.d.members.metadata,*s.d.dex_registry,s.cleanup.get());
 if(s.d.boot_catalog){
  if(!s.boot_catalog.Import(*s.d.boot_catalog,s.context,*s.d.roots,s.pool,s.d.members.metadata,[](const void*owner,void*method)noexcept{return static_cast<const modern_field_metadata::FieldMetadata*>(owner)->MethodDeclaringClass(method);},error))return Fail(error,"Source boot catalog root import refused");
  s.frame_storage.boot_catalog=&s.boot_catalog;s.frame_storage.boot_reader=s.ledger.get();
 }
 s.mapper=std::make_unique<native_hierarchy_source_cache::Mapper>(s.context,s.pool,*s.d.members.metadata,*s.d.members.fields,s.access,s.ledger.get());
 s.caches=std::make_unique<native_hierarchy_source_cache::Composite>(*s.mapper,s.incoming);
 void*cache=nullptr,*expected=nullptr;
 if(!w.strong_cache||!w.strong_expected_class||!s.d.roots->BorrowOwnedStrongGlobal(s.context,w.strong_cache,&cache))return Fail(error,"Source type-cache independent strong oracle unavailable");
 void*cache_root=s.pool.Add(cache,error);if(!cache_root||!s.d.roots->BorrowOwnedStrongGlobal(s.context,w.strong_expected_class,&expected))return false;
 void*expected_root=s.pool.Add(expected,error);if(!expected_root)return false;
 native_hierarchy_cache::Witness witness{w.type_index,w.field_index,w.method_index,expected_root,w.native_field,w.native_method};
 if(w.resolve&&!w.resolve(w.resolution_owner,s.context,s.pool,cache_root,expected_root,error))return false;
 if(w.public_storage&&!w.public_storage(w.resolution_owner,s.context,&witness,error))return false;
 if(!s.incoming.Calibrate(*s.mapper,cache_root,w.extent,w.counts,witness,error))return false;
 native_hierarchy_graph::Bindings graph{s.d.members.metadata,s.d.roots,[](void*owner,const runnable_jni::Context&c,jobject g,void**out){return static_cast<source_handle_roots::Service*>(owner)->BorrowOwnedStrongGlobal(c,g,out);},s.d.allocator,&s.receivers,s.caches.get(),[](void*owner,const runnable_jni::Context&c,jobject a,jobject b,void**left,void**right){return static_cast<source_handle_roots::Service*>(owner)->BorrowOwnedStrongPair(c,a,b,left,right);}};
 if(!s.d.registry->BindNative(&s,State::RootInsert,State::RootRead,State::RootClear,State::RootEpoch))return Fail(error,"Actual schema native root registry owner refused");
 if(!s.authority.Stage(s.context,ti,inputs,graph,error))return false;
 native_hierarchy_session::Bindings b{s.driver,s.context,s.d.hooks,s.d.members.metadata,s.d.roots,&s.pool,&s.authority,s.d.allocator,&s.access,native_hierarchy_source_checks::RefuseActualClass,native_owned_removal::ObserveSourceFailure,s.ledger.get()};
 s.hierarchy=std::make_unique<native_hierarchy_session::Session>(b);
 if(!s.hierarchy->Ready())return Fail(error,"Actual original schema Session did not accept staged graph");
 s.phase=State::Phase::Running;return true;
}
RunState::~RunState(){
 auto&s=*state_;Require(s.phase==State::Phase::Constructed||s.phase==State::Phase::Ended);
 s.d.registry->Clear();s.facade.reset();s.hierarchy.reset();
 if(s.phase==State::Phase::Constructed){if(s.ledger)s.ledger->ReleaseAfterRun(false);if(s.loading)s.loading->Complete(false);}
 s.frame_storage.boot_unaffected=nullptr;s.boot_unaffected.reset();
 s.caches.reset();s.mapper.reset();s.ledger.reset();s.loading.reset();s.cleanup.reset();s.stop.reset();
 s.authority.Clear(s.context.env);
 if(current==this)current=s.previous;
}
RunState* RunState::Current()noexcept{return current;}
abi::arm64::android11::ReleaseObjectPointer RunState::FindOriginal(void*linker,void*self,const char*descriptor,void*loader){
 auto*r=current;if(!r)return {0};const auto&s=*r->state_;
 if(linker!=s.d.members.runtime.receivers.class_linker||self!=s.context.self||!descriptor||!loader||s.phase==State::Phase::Ended)return {0};
 return s.d.lookup->Find(s.context,descriptor,loader);
}
bool RunState::BeforeVerification(void*o,const transaction_root::Iterator*i,std::string*e){auto&s=*state_;return s.phase==State::Phase::Running&&s.hierarchy&&s.hierarchy->BeforeVerification(o,i,e);}
void RunState::AfterVerification(void*o,const transaction_root::Iterator*i,bool result)noexcept{auto&s=*state_;Require(s.hierarchy!=nullptr);s.hierarchy->AfterVerification(o,i,result);}
struct RunState::StoppedAttempt {
 RunState* run;std::string* error;
 enum class Failure {None,Allocation,Unexpected};
 Failure failure=Failure::None;
};
bool RunState::PrepareOriginalWaiting(void*raw,native_source_stop::Session&stop)noexcept{
 auto*a=static_cast<StoppedAttempt*>(raw);
 if(!a||!a->run||current!=a->run||a->run->state_->stop.get()!=&stop)return false;
 // No C++ exception may unwind through the original ART Mutex::Unlock body.
 // Report allocation errors only once StartGC has restored actual Runnable.
 try{return a->run->PrepareStopped(a->error);}
 catch(const std::bad_alloc&){a->failure=StoppedAttempt::Failure::Allocation;}
 catch(...){a->failure=StoppedAttempt::Failure::Unexpected;}
 return false;
}
bool RunState::Collected(const native_owned_removal::RootedGraph&g,source_commit::Plan&p,std::string*e){
 auto&s=*state_;
 if(s.phase!=State::Phase::Running)return Fail(e,"Collected schema requires the running transaction phase");
 if(!g.holder)return Fail(e,"Collected schema has no actual transaction root holder");
 if(!s.loading->Paused())return Fail(e,"Collected schema requires the paused source loading owner");
 if(!s.cleanup->AttachHolder(static_cast<const transaction_root::Holder*>(g.holder),g.metadata))return Fail(e,"Collected cleanup rejected actual holder/driver metadata");
 if(!s.hierarchy->PrepareCollected(p,e)||!s.authority.PrepareRoots(p,e))return false;
 if(!s.d.frames->PrepareRecoveryPlan(p,&s.frame_storage))return Fail(e,"Ordinary source frame exact Plan preparation refused");
 if(s.d.boot_catalog&&s.d.boot_vtables&&s.d.boot_code&&s.d.members.method_model){
  s.boot_unaffected=std::make_unique<native_boot_aot_unaffected::Source>(p,*s.stop,s.context.self,s.boot_catalog,*s.ledger,*s.d.members.metadata,*s.d.members.method_model,*s.d.boot_vtables,*s.d.boot_code);
  s.frame_storage.boot_unaffected=s.boot_unaffected.get();
 }
 s.plan=&p;s.holder=g.holder;size_t publication_count=0;
 if(!s.ledger->PreparePublication(*s.stop,&publication_count,e)||!s.stop->PrepareDexPublication(s.driver,s.holder,s.d.members.runtime.receivers.class_linker,publication_count,{this,MatchDexPublication},e))return false;
 const auto collect=s.d.registry->Collect();
 if(!rooted_schema_plan::CollectBindings(&p,collect,&s.plan_roots)||!s.ledger->CollectManagedRoots(collect)||!s.authority.CollectManagedRoots(collect)||(s.d.guards&&!s.d.guards->CollectManagedRoots(collect,e))||!s.d.registry->Freeze())return Fail(e,"Ordinary schema roots did not register completely");
 if(s.d.stop->RequiresOriginalWaiting()){
  StoppedAttempt attempt{this,e};
  const bool accepted=s.stop->BeginOriginalWaiting(s.context.self,s.extent,&attempt,PrepareOriginalWaiting);
  if(attempt.failure==StoppedAttempt::Failure::Allocation)return Fail(e,"Stopped schema preparation exhausted native memory");
  if(attempt.failure==StoppedAttempt::Failure::Unexpected)return Fail(e,"Stopped schema preparation threw");
  if(!accepted)return e&&!e->empty()?false:Fail(e,s.stop->Failure());
 }else{
  if(!s.stop->Begin(s.context.self,s.extent))return Fail(e,s.stop->Failure());
  if(!PrepareStopped(e))return false;
 }
 // The waiting path restores Runnable in the original StartGC scope before
 // this callback returns. Both paths now retain the same owned reservation.
 s.phase=State::Phase::Collected;return true;
}
bool RunState::PrepareStopped(std::string*e){
 auto&s=*state_;
 if(current!=this||s.phase!=State::Phase::Running||!s.plan||!s.holder||!s.stop||!s.stop->IsCollectedStop(s.context.self))return Fail(e,"Schema preparation lacks the exact collected stop owner");
 auto&p=*s.plan;
 if(s.d.mapping.extent){
  if(!s.d.mapping.refresh_stopped(s.d.mapping.owner,e))return false;
  s.read_context={this,MappingExtent};s.d.members.read_context=&s.read_context;s.mapper->UseReadContext(s.read_context);s.receivers.UseReadContext(s.read_context);s.authority.UseReadContext(s.read_context);
  if(!s.cleanup->UseRecoveryContext(s.read_context))return Fail(e,"Cleanup refused actual stopped frame recovery context");
  if(s.d.guards&&!s.d.guards->UseStoppedReadContext(&s.read_context))return Fail(e,"Frozen schema verifier refused actual stopped read context");
 }
 // From here no JNI root can be created. Existing binds only known identities.
 if(!s.d.registry->Resolve())return Fail(e,"Stopped schema root resolution failed");const auto resolve=s.d.registry->Final();
 if(!s.plan_roots.CanRefresh(resolve)||!s.ledger->CanRefresh(resolve)||!s.authority.CanRefresh(resolve))return Fail(e,"Stopped schema root locations cannot refresh");
 s.plan_roots.Refresh(resolve);s.ledger->Refresh(resolve);s.authority.Refresh(resolve);
 if(!s.incoming.Refresh()||(s.d.guards&&!s.d.guards->RefreshManagedRoots(p,resolve))||!s.cleanup->PrepareCollected(p,e)||!s.hierarchy->Collected(p,e))return false;
 if(s.d.guards&&!s.d.guards->UseHierarchyAuthority(this))return Fail(e,"Frozen schema guard rejected the actual sealed hierarchy authority");
 auto native=s.d.provider->Prepare(p,s.d.members,e);if(!native)return false;
 if(!s.d.provider->CollectManagedRoots(*native,s.d.registry->Existing())){
  if(auto* concrete=dynamic_cast<native_source_commit::Services*>(native.get()))s.d.provider->ReleasePreparedSource(*concrete);
  return Fail(e,"Native provider attempted an unregistered stopped root");
 }
 s.facade=s.cleanup->Prepare(std::move(native),p,e);if(!s.facade)return false;
 if(!s.stop->ContinueSource())return Fail(e,s.stop->Failure());
 return true;
}
std::unique_ptr<source_commit::Services> RunState::Finalize(const void*h,const source_commit::Plan**p)noexcept{
 auto&s=*state_;if(!p||s.phase!=State::Phase::Collected||h!=s.holder||!s.facade||!s.stop->IsFinalWindow(s.context.self)||!s.d.registry->Resolve())return nullptr;
 const auto r=s.d.registry->Final();if(!s.plan_roots.CanRefresh(r)||!s.ledger->CanRefresh(r)||!s.authority.CanRefresh(r)||(s.d.guards&&!s.d.guards->CanRefreshManagedRoots(r)))return nullptr;
 s.plan_roots.Refresh(r);s.ledger->Refresh(r);s.authority.Refresh(r);
 if(!s.incoming.Refresh()||(s.d.guards&&!s.d.guards->RefreshManagedRoots(*s.plan,r))||!s.d.provider->RefreshManagedRoots(*s.plan,s.facade->Delegate(),r)||!s.cleanup->RefreshFinal(r))return nullptr;
 *p=s.plan;s.phase=State::Phase::Finalized;return std::move(s.facade);
}
void RunState::Commit(const source_commit::Plan&p)noexcept{auto&s=*state_;Require(s.phase==State::Phase::Finalized&&s.plan==&p&&s.cleanup->CommitObserver(p));s.hierarchy->BeginCommit(p);s.phase=State::Phase::Committed;}
void RunState::Refund()noexcept{
 auto&s=*state_;if(s.phase==State::Phase::Refunded)return;
 Require(s.phase!=State::Phase::Committed&&s.phase!=State::Phase::Ended);
 // A retained continuation cannot fail after original publication. Before
 // ContinueSource, rollback restores actual Runnable before retirement/JNI.
 Require(s.phase!=State::Phase::Collected&&s.phase!=State::Phase::Finalized);
 // Ledger and allocator stopped-root readers borrow cleanup, which in turn
 // borrows stop. Revoke the live stop before retirement/JNI, retain both
 // witnesses until all hierarchy refund readers have returned.
 if(s.stop)Require(s.stop->RollbackBeforeSource());
 s.facade.reset();
 if(s.hierarchy&&s.hierarchy->Ready())s.hierarchy->Refund();
 s.frame_storage.boot_unaffected=nullptr;s.boot_unaffected.reset();
 s.cleanup.reset();s.stop.reset();
 s.phase=State::Phase::Refunded;
}
bool RunState::End(void*d,bool success)noexcept{
 auto&s=*state_;Require(d==s.driver&&current==this&&s.phase!=State::Phase::Ended);
 const bool ok=success?s.phase==State::Phase::Committed&&s.stop&&s.stop->Completed()&&s.cleanup->Completed():s.phase==State::Phase::Refunded;
 Require(ok);if(s.hierarchy)Require(s.hierarchy->ReleaseAfterRun(success));
 const bool loading_proof=s.loading->Complete(success);Require(!success||loading_proof);
 s.phase=State::Phase::Ended;return ok;
}
bool RunState::MatchDexPublication(void*raw,void*driver,const void*holder,void*linker,void*cache,void*loader,size_t*index)noexcept{
 auto*r=static_cast<RunState*>(raw);if(!r||current!=r)return false;const auto&s=*r->state_;
 return s.phase==State::Phase::Collected&&s.stop&&s.stop->CanPublishDex(driver,holder,linker)&&s.ledger&&s.ledger->MatchPublication(driver,holder,linker,cache,loader,index);
}
bool RunState::MappingExtent(void*raw,const void*p,size_t*out)noexcept{auto*r=static_cast<RunState*>(raw);return r&&r==current&&r->StoppedWindow()&&r->state_->d.mapping.extent&&r->state_->d.mapping.extent(r->state_->d.mapping.owner,p,out);}
bool RunState::StoppedWindow()const noexcept{return current==this&&state_->stop&&state_->stop->IsStopped(state_->context.self);}
bool RunState::VerifyHierarchy(const source_commit::Plan& p,size_t pair,void* loader)const noexcept {
 const auto& s=*state_;
 if(current!=this||s.plan!=&p||pair>=p.old_classes.size()||!s.stop||
    (s.phase!=State::Phase::Running&&s.phase!=State::Phase::Collected&&s.phase!=State::Phase::Finalized)||
    !s.stop->IsStopped(s.context.self)||!redefinition_gate::Owned(static_cast<JavaVM*>(s.context.vm)))return false;
 const auto* graph=s.authority.Staged();if(!graph)return false;
 size_t matches=0;for(const auto& node:graph->nodes)if(node.affected&&node.klass==p.old_classes[pair]&&node.loader==loader)++matches;
 return matches==1&&s.authority.Verify(p,pair);
}
const modern_field_metadata::ReadContext* RunState::CurrentReadContext()const noexcept{return StoppedWindow()&&state_->read_context.extent?&state_->read_context:nullptr;}
const whole_process_frames::Report& RunState::Frames()const noexcept{Require(state_->cleanup!=nullptr);return state_->cleanup->Frames();}
}
