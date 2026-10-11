#include "platform/common/readable_mapping_scope.h"
#include <android/log.h>
#include <optional>
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
namespace {
// Called only after original stop rollback or StartGC restored Runnable.
// Report fields are copied scalars; no stack memory, JNI or live frame reads.
void LogFrameReport(const whole_process_frames::Report& frames,bool accepted)noexcept{
  __android_log_print(ANDROID_LOG_INFO,"artdcevm",
    "Owned stopped frame report prepareAccepted=%d frameAccepted=%d frames=%zu runtime=%zu failure=%s thread=%p method=%p quick=%p pc=%p header=%p prefix=%d physical=%d declaring=%u flags=%u dexIndex=%u mapRow=%u mapMask=%u mapIndex=%u mapDexPc=%u dexRegs=%u inlineBegin=%u inlineEnd=%u capacity=%zu rootFailure=%u rootStreamFailure=%u rootFrames=%zu rootSlots=%zu rootCallbacks=%zu upcallThreads=%zu upcallClosed=%zu upcallFragments=%zu upcallTransitions=%zu upcallFailure=%u frameBytes=%u stackBit=%u",
    int(accepted),int(frames.accepted),frames.frames,frames.runtime_frames,frames.failure,
    reinterpret_cast<void*>(frames.failure_thread),reinterpret_cast<void*>(frames.failure_method),
    reinterpret_cast<void*>(frames.failure_quick),reinterpret_cast<void*>(frames.failure_pc),reinterpret_cast<void*>(frames.failure_header),
    int(frames.failure_method_prefix),int(frames.failure_physical),frames.failure_declaring_class,frames.failure_flags,frames.failure_dex_index,
    frames.failure_map_row,frames.failure_map_mask,frames.failure_map_index,frames.failure_map_dex_pc,frames.failure_map_dex_regs,
    frames.failure_inline_begin,frames.failure_inline_end,frames.failure_map_capacity,
    frames.root_failure,frames.root_stream_failure,frames.root_frames,frames.root_slots,frames.root_callbacks,
    frames.upcall_threads,frames.upcall_closed,frames.upcall_fragments,frames.upcall_transitions,frames.upcall_failure,frames.failure_frame_bytes,frames.failure_stack_bit);
  __android_log_print(ANDROID_LOG_INFO,"artdcevm",
    "Owned stopped callee diagnostics callerThread=%p callerMethod=%p callerQuick=%p callerPc=%p youngerPresent=%d previousKind=%u youngerMethod=%p youngerQuick=%p youngerPc=%p receiverState=%u registers=%u ins=%u thisIndex=%u mapFirst=%u mapCount=%u locationKind=%d locationValue=%u receiverSlot=%p receiverValue=%p",
    reinterpret_cast<void*>(frames.receiver_caller_thread),reinterpret_cast<void*>(frames.receiver_caller_method),reinterpret_cast<void*>(frames.receiver_caller_quick),reinterpret_cast<void*>(frames.receiver_caller_pc),
    int(frames.younger_present),frames.previous_kind,reinterpret_cast<void*>(frames.younger_method),reinterpret_cast<void*>(frames.younger_quick),reinterpret_cast<void*>(frames.younger_pc),
    frames.receiver_state,frames.receiver_registers,frames.receiver_ins,frames.receiver_this_index,frames.receiver_map_first,frames.receiver_map_count,
    frames.receiver_location_kind,frames.receiver_location_value,reinterpret_cast<void*>(frames.receiver_slot),reinterpret_cast<void*>(frames.receiver_value));
  __android_log_print(ANDROID_LOG_INFO,"artdcevm",
    "Owned stopped runtime name status=%u thread=%p method=%p quick=%p name=%s",
    frames.runtime_name_status,reinterpret_cast<void*>(frames.runtime_name_thread),reinterpret_cast<void*>(frames.runtime_name_method),
    reinterpret_cast<void*>(frames.runtime_name_quick),frames.runtime_name);
  if(frames.aot_verify_failure)
    __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned stopped AOT Source rejection thread=%p method=%p dexPc=%u reason=%s",
      reinterpret_cast<void*>(frames.aot_verify_thread),reinterpret_cast<void*>(frames.aot_verify_method),frames.aot_verify_dex_pc,frames.aot_verify_failure);
  if(frames.aot_declaring_class&&frames.aot_method&&frames.aot_descriptor)
    __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned stopped AOT identity owner=%s method=%s descriptor=%s",
      frames.aot_declaring_class,frames.aot_method,frames.aot_descriptor);
}
}
struct RunState::State {
 RunState* owner=nullptr;native_source_stop::Session* inspection=nullptr;
 Dependencies d;modern_field_metadata::ReadContext read_context{},inspection_context{};void* driver;const runnable_jni::Context context;
 source_handle_roots::Pool pool;native_hierarchy_source_cache::AllocatorBatchAccess access;
 native_hierarchy_graph::Authority authority;native_hierarchy_receiver::Preservation receivers;
 std::unique_ptr<native_hierarchy_typed_storage::Proof> typed_storage;
 native_boot_method_catalog::Rooted boot_catalog;
 std::unique_ptr<native_boot_aot_unaffected::Source> boot_unaffected;
 std::unique_ptr<native_interface_dispatch::Source> interface_dispatch;
 std::unique_ptr<native_frame_root_relocation::Proof> frame_roots;
 whole_process_frames::FrameRootBinding frame_root_binding;
 prepared_roots::Resolver frame_root_resolver;
 void RevokeFrameOwners()noexcept {
  frame_storage.frame_roots=nullptr;frame_storage.root_provenance=nullptr;
  frame_storage.dispatch=nullptr;frame_storage.continuation=nullptr;
  frame_storage.source_code=nullptr;
  frame_storage.managed_stack_source=nullptr;frame_storage.exception_calibration=nullptr;
  frame_storage.exception_vm=nullptr;frame_storage.upcall_native_owner=nullptr;
  frame_storage.frame_root_resolver=nullptr;frame_storage.boot_unaffected=nullptr;
  d.members.frame_root_proof=nullptr;d.members.frame_roots=nullptr;d.members.frame_root_binding=nullptr;
  frame_root_binding.Clear();frame_root_resolver={};
  interface_dispatch.reset();frame_roots.reset();boot_unaffected.reset();
 }
 whole_process_frames::Service::Prepared frame_storage;
 std::unique_ptr<native_source_stop::Session> stop;
 std::unique_ptr<native_required_cleanup::Service> cleanup;
 std::unique_ptr<native_hierarchy_private_ledger::Ledger> ledger;
 std::unique_ptr<native_hierarchy_source_cache::Mapper> mapper;
 native_hierarchy_cache::Owner incoming;source_cache_public_roles::Rooted public_roles;
 std::unique_ptr<native_hierarchy_source_cache::Composite> caches;
 std::unique_ptr<native_source_class_loading::Run> loading;
 std::unique_ptr<native_hierarchy_session::Session> hierarchy;
 std::unique_ptr<native_required_cleanup::Facade> facade;
 prepared_roots::Bindings plan_roots;
 source_commit::Plan* plan=nullptr;const void* holder=nullptr;
 size_t extent=0;RunState* previous=nullptr;
 enum class Phase {Constructed,Running,Collected,Finalized,Committed,Refunded,Ended};
 Phase phase=Phase::Constructed;
 struct InspectionReader final:source_handle_roots::StoppedReader {
  const State& state;explicit InspectionReader(const State&s):state(s){}
  bool FinalRootWindow()const noexcept override{return state.owner==current&&state.inspection&&state.inspection->IsStopped(state.context.self);}
  bool ReadStoppedRoot(const void* token,void** value)const noexcept override{return state.pool.ReadStopped(*this,token,value);}
 } inspection_roots{*this};
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
  if(s.inspection_roots.FinalRootWindow())return s.inspection_roots.ReadStoppedRoot(reinterpret_cast<const void*>(slot),value);
  RootReader stopped(s);
  return stopped.FinalRootWindow()?stopped.ReadStoppedRoot(reinterpret_cast<const void*>(slot),value):
      RootEpoch(raw)&&s.pool.Read(reinterpret_cast<const void*>(slot),value);
 }
 static void RootClear(void*){}
 State(const Dependencies& deps,void* dr,const runnable_jni::Context&ctx)
  :d(deps),driver(dr),context(ctx),pool(*deps.roots),access(pool),receivers(*deps.members.metadata){}
};
RunState::RunState(const Dependencies&d,void*driver,const runnable_jni::Context&c):state_(new State(d,driver,c)){state_->owner=this;state_->inspection_context={this,MappingExtent};}
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
  __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned RunState stage phase=boot-root-import-begin roots=%zu methods=%zu",s.d.boot_catalog->Roots().size(),s.d.boot_catalog->Methods().size());
  if(!s.boot_catalog.Import(*s.d.boot_catalog,s.context,*s.d.roots,s.pool,s.d.members.metadata,[](const void*owner,void*method)noexcept{return static_cast<const modern_field_metadata::FieldMetadata*>(owner)->MethodDeclaringClass(method);},error))return Fail(error,"Source boot catalog root import refused");
  __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned RunState stage phase=boot-root-import-return");
  s.frame_storage.boot_catalog=&s.boot_catalog;s.frame_storage.boot_reader=s.ledger.get();
 }
 s.mapper=std::make_unique<native_hierarchy_source_cache::Mapper>(s.context,s.pool,*s.d.members.metadata,*s.d.members.fields,s.access,s.ledger.get(),w.semantic);
 if(!s.mapper->BindSemanticInspection(this,InspectSemantic))return Fail(error,"Source semantic inspection controller binding failed");
 s.caches=std::make_unique<native_hierarchy_source_cache::Composite>(*s.mapper,s.incoming);
 __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned RunState stage phase=incoming-cache-begin");
 void*cache=nullptr,*expected=nullptr;
 if(!w.strong_cache||!w.strong_expected_class||!s.d.roots->BorrowOwnedStrongGlobal(s.context,w.strong_cache,&cache))return Fail(error,"Source type-cache independent strong oracle unavailable");
 void*cache_root=s.pool.Add(cache,error);if(!cache_root)return Fail(error,"Source cache strong oracle could not enter actual root pool");
 if(!s.d.roots->BorrowOwnedStrongGlobal(s.context,w.strong_expected_class,&expected))return Fail(error,"Source cache expected Class strong oracle was rejected");
 void*expected_root=s.pool.Add(expected,error);if(!expected_root)return Fail(error,"Source cache expected Class could not enter actual root pool");
 native_hierarchy_cache::Witness witness{w.type_index,w.field_index,w.method_index,expected_root,w.native_field,w.native_method};
 if(w.resolve&&!w.resolve(w.resolution_owner,s.context,s.pool,cache_root,expected_root,error)){if(error&&error->empty())*error="Source cache original resolution callback rejected independent root witness";return false;}
 witness.semantic_resolution_proved=w.resolve!=nullptr;
 if(!w.public_roles||!s.public_roles.Import(*w.public_roles,s.context,*s.d.roots,s.pool,error))return Fail(error,"Source cache complete independent public roles import failed");
 witness.current_roles=&s.public_roles;
 if(w.public_storage&&!w.public_storage(w.resolution_owner,s.context,&witness,error)){if(error&&error->empty())*error="Source cache public storage callback rejected original storage witness";return false;}
 if(!s.incoming.Calibrate(*s.mapper,cache_root,w.extent,w.counts,witness,error)){if(error&&error->empty())*error="Source incoming cache calibration rejected actual independent witness";return false;}
 __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned RunState stage phase=incoming-cache-return");
 if(!s.d.typed_heap||!s.d.typed_heap->Calibrated())return Fail(error,"Schema source Run requires independent typed heap calibration");
 s.typed_storage=std::make_unique<native_hierarchy_typed_storage::Proof>(*s.d.typed_heap,*s.d.roots,*s.d.members.metadata);
 // Public Field.getType can load Classes. Finish its complete fixed point
 // before the hierarchy owner snapshots the actual loaded-Class graph.
 __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned RunState stage phase=typed-inventory-begin");
 const bool typed_staged=s.typed_storage->Stage(*s.d.hooks,s.context,ti,error);
 __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned RunState stage phase=typed-inventory-return accepted=%d",int(typed_staged));
 if(!typed_staged)return false;
 native_hierarchy_graph::Bindings graph{s.d.members.metadata,s.d.roots,[](void*owner,const runnable_jni::Context&c,jobject g,void**out){return static_cast<source_handle_roots::Service*>(owner)->BorrowOwnedStrongGlobal(c,g,out);},s.d.allocator,&s.receivers,s.caches.get(),[](void*owner,const runnable_jni::Context&c,jobject a,jobject b,void**left,void**right){return static_cast<source_handle_roots::Service*>(owner)->BorrowOwnedStrongPair(c,a,b,left,right);}};
 if(!s.d.registry->BindNative(&s,State::RootInsert,State::RootRead,State::RootClear,State::RootEpoch))return Fail(error,"Actual schema native root registry owner refused");
 __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned RunState stage phase=hierarchy-inventory-begin");
 const bool hierarchy_staged=s.authority.Stage(s.context,ti,inputs,graph,error);
 __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned RunState stage phase=hierarchy-inventory-return accepted=%d",int(hierarchy_staged));
 if(!hierarchy_staged){if(error&&error->empty())*error="Source hierarchy authority staging rejected actual public graph";return false;}
 native_hierarchy_session::Bindings b{s.driver,s.context,s.d.hooks,s.d.members.metadata,s.d.roots,&s.pool,&s.authority,s.d.allocator,&s.access,native_hierarchy_source_checks::RefuseActualClass,native_owned_removal::ObserveSourceFailure,s.ledger.get(),this,CompleteRefund};
 s.hierarchy=std::make_unique<native_hierarchy_session::Session>(b);
 if(!s.hierarchy->Ready())return Fail(error,"Actual original schema Session did not accept staged graph");
 s.phase=State::Phase::Running;return true;
}
RunState::~RunState(){
 auto&s=*state_;Require(s.phase==State::Phase::Constructed||s.phase==State::Phase::Ended);
 s.d.registry->Clear();s.facade.reset();s.hierarchy.reset();
 if(s.phase==State::Phase::Constructed){if(s.ledger)s.ledger->ReleaseAfterRun(false);if(s.loading)s.loading->Complete(false);}
 s.RevokeFrameOwners();
 s.caches.reset();s.mapper.reset();s.ledger.reset();s.loading.reset();s.cleanup.reset();s.stop.reset();
 s.authority.Clear(s.context.env);
 if(s.typed_storage)s.typed_storage->Clear(s.context.env);
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
struct RunState::FrameCensusAttempt {
 RunState& run;whole_process_frames::Service::Census census;
};
bool RunState::CensusFramesStopped(void*raw,native_source_stop::Session&stop)noexcept {
 auto&request=*static_cast<FrameCensusAttempt*>(raw);auto&r=request.run;auto&s=*r.state_;
 if(current!=&r||s.inspection||!s.plan||!stop.IsStopped(s.context.self))return false;
 s.inspection=&stop;
 // The temporary stop's resolved pointers must expire before the original
 // StartGC resumes Runnable and permits another collection. The final stop
 // resolves the retained root slots again before any consumer reads them.
 struct Reset {State& state;~Reset(){state.d.registry->Invalidate();state.inspection=nullptr;}} reset{s};
 try{
  request.census.failure="census-current-root-resolution-or-mapping-refused";
  if(!s.d.mapping.refresh_stopped(s.d.mapping.owner,nullptr)||!s.d.registry->Resolve())return false;
  const auto roots=s.d.registry->Final();
  if(!s.plan_roots.CanRefresh(roots))return false;
  s.plan_roots.Refresh(roots);
  request.census=s.d.frames->CensusRecovery(stop,s.context.self,*s.d.members.cache,s.frame_storage,s.inspection_context);
  return request.census.accepted;
 }catch(...){return false;}
}
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
 if(current!=this||!s.pool.ReadyForSourceAccess()||!s.d.registry->IsNativeBoundTo(&s))return Fail(e,"Collected ordinary root registration lost its exact Runnable/native storage owner");
 // Native Pool registration never invokes JNI or an original ART callee. Keep
 // one fresh mapping snapshot only for this ordinary read/root insertion cohort;
 // every Pool slot still checks its actual source Thread and owned slot range.
 // Destroy the view before entering sourceStop (which can safepoint/collect).
 std::optional<method_identity::ScopedReadableMappings> ordinary;
 ordinary.emplace();if(!ordinary->Ready())return Fail(e,"Collected ordinary root mapping capture failed");
 const auto checkpoint=[](const char* stage){__android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned RunState Collected phase=%s",stage);};
 checkpoint("attach-holder-begin");
 if(!s.cleanup->AttachHolder(static_cast<const transaction_root::Holder*>(g.holder),g.metadata))return Fail(e,"Collected cleanup rejected actual holder/driver metadata");
 checkpoint("attach-holder-return");checkpoint("hierarchy-collected-begin");
 if(!s.hierarchy->PrepareCollected(p,e))return false;
 checkpoint("hierarchy-collected-return");checkpoint("authority-roots-begin");
 if(!s.authority.PrepareRoots(p,e))return false;
 checkpoint("authority-roots-return");checkpoint("frame-plan-begin");
 if(!s.d.frames->PrepareRecoveryPlan(p,&s.frame_storage))return Fail(e,"Ordinary source frame exact Plan preparation refused");
 checkpoint("frame-plan-return");
 if(s.d.boot_catalog&&s.d.boot_vtables&&s.d.boot_code&&s.d.members.method_model){
  s.boot_unaffected=std::make_unique<native_boot_aot_unaffected::Source>(p,*s.stop,s.context.self,s.boot_catalog,*s.ledger,*s.d.members.metadata,*s.d.members.method_model,*s.d.boot_vtables,*s.d.boot_code);
  s.frame_storage.boot_unaffected=s.boot_unaffected.get();
 }
 s.plan=&p;s.holder=g.holder;size_t publication_count=0;checkpoint("publication-plan-begin");
 if(!s.ledger->PreparePublication(*s.stop,&publication_count,e)||!s.stop->PrepareDexPublication(s.driver,s.holder,s.d.members.runtime.receivers.class_linker,publication_count,{this,MatchDexPublication},e))return false;
 checkpoint("publication-plan-return");checkpoint("native-root-registration-begin");
 const auto collect=s.d.registry->Collect();
 if(!rooted_schema_plan::CollectBindings(&p,collect,&s.plan_roots)||!s.ledger->CollectManagedRoots(collect)||!s.authority.CollectManagedRoots(collect)||!s.typed_storage||!s.typed_storage->Capture(p,collect,e)||(s.d.guards&&!s.d.guards->CollectManagedRoots(collect,e)))return Fail(e,"Ordinary schema roots did not register completely");
 if(s.boot_unaffected&&s.d.frame_roots){
  s.frame_roots=std::make_unique<native_frame_root_relocation::Proof>();
  if(!s.frame_roots->PrepareIdentity(p,collect))return Fail(e,"Ordinary frame relocation Plan roots did not register");
  const auto&layout=s.d.members.method_model->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
  s.interface_dispatch=std::make_unique<native_interface_dispatch::Source>(*s.d.members.metadata,layout,s.boot_catalog,*s.ledger);
  s.frame_storage.source_code=s.d.boot_code;
  if(s.d.exception_calibration&&s.d.exception_calibration->MatchesVm(static_cast<JavaVM*>(s.context.vm))&&s.d.upcall_native_owner){
   s.frame_storage.managed_stack_source=s.d.roots;
   s.frame_storage.exception_calibration=s.d.exception_calibration;
   s.frame_storage.exception_vm=static_cast<JavaVM*>(s.context.vm);
   s.frame_storage.upcall_native_owner=s.d.upcall_native_owner;
  }
 }
 if(!s.d.registry->Freeze())return Fail(e,"Ordinary schema root registry could not freeze");
 checkpoint("native-root-registration-return");
 ordinary.reset();
 if(s.frame_roots){
  FrameCensusAttempt request{*this,{}};native_source_stop::Session inspection(*s.d.stop);
  const bool counted=s.d.stop->RequiresOriginalWaiting()?inspection.InspectOriginalWaiting(s.context.self,s.extent,&request,CensusFramesStopped):inspection.Inspect(s.context.self,s.extent,&request,CensusFramesStopped);
  if(!counted){LogFrameReport(request.census.diagnostic,false);return Fail(e,request.census.accepted?inspection.Failure():request.census.failure);}
  size_t units=0;for(const auto&method:s.d.boot_catalog->Methods())units=std::max(units,method.dex.size()/2);
  units=std::max(units,request.census.code_units);
  if(!s.frame_roots->ReserveFramesSlots(request.census.frames,request.census.slots)||!s.frame_storage.PrepareContinuations(request.census.logical_frames,units,request.census.tries,request.census.handlers))return Fail(e,"Ordinary complete frame/continuation capacity preparation refused");
  if(s.frame_storage.managed_stack_source&&!s.d.frames->PrepareUpcalls(request.census,*s.d.roots,&s.frame_storage))return Fail(e,"Ordinary native upcall source preparation refused");
  s.frame_storage.frame_roots=s.frame_roots.get();s.frame_storage.root_provenance=s.d.frame_roots;
  s.frame_storage.dispatch=s.interface_dispatch.get();s.frame_storage.continuation=s.boot_unaffected.get();
 }
 checkpoint("source-stop-begin");
 if(s.d.stop->RequiresOriginalWaiting()){
  StoppedAttempt attempt{this,e};
  const bool accepted=s.stop->BeginOriginalWaiting(s.context.self,s.extent,&attempt,PrepareOriginalWaiting);
  // Original StartGC has restored Runnable.
  LogFrameReport(s.cleanup->Frames(),accepted);
  if(attempt.failure==StoppedAttempt::Failure::Allocation)return Fail(e,"Stopped schema preparation exhausted native memory");
  if(attempt.failure==StoppedAttempt::Failure::Unexpected)return Fail(e,"Stopped schema preparation threw");
  if(!accepted)return e&&!e->empty()?false:Fail(e,s.stop->Failure());
 }else{
  if(!s.stop->Begin(s.context.self,s.extent))return Fail(e,s.stop->Failure());
  if(!PrepareStopped(e))return false;
 }
 // The waiting path restores Runnable in the original StartGC scope before
 // this callback returns. Both paths now retain the same owned reservation.
 checkpoint("source-stop-preparation-return");
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
 if(!s.plan_roots.CanRefresh(resolve)||!s.ledger->CanRefresh(resolve)||!s.authority.CanRefresh(resolve)||!s.typed_storage||!s.typed_storage->CanRefresh(resolve))return Fail(e,"Stopped schema root locations cannot refresh");
 s.plan_roots.Refresh(resolve);s.ledger->Refresh(resolve);s.authority.Refresh(resolve);
 s.typed_storage->Refresh(resolve);
 if(s.frame_roots){
  s.frame_root_resolver=resolve;s.frame_storage.frame_root_resolver=&s.frame_root_resolver;
  if(!s.d.frames->BindFrameRoots(*s.stop,s.context.self,*s.d.members.cache,s.frame_storage,p,s.read_context,*s.frame_roots,&s.frame_root_binding))return Fail(e,"Collected actual frame root binding refused");
  s.d.members.frame_root_proof=s.frame_roots.get();s.d.members.frame_roots=s.d.frame_roots;s.d.members.frame_root_binding=&s.frame_root_binding;
 }
 if(!s.incoming.Refresh()||(s.d.guards&&!s.d.guards->RefreshManagedRoots(p,resolve))||!s.cleanup->PrepareCollected(p,e)||!s.hierarchy->Collected(p,e))return false;
 if(s.d.guards&&!s.d.guards->UseHierarchyAuthority(this))return Fail(e,"Frozen schema guard rejected the actual sealed hierarchy authority");
 auto native=s.d.provider->Prepare(p,s.d.members,e);if(!native)return false;
 if(!s.d.provider->CollectManagedRoots(*native,s.d.registry->Existing())){
  if(auto* concrete=dynamic_cast<native_source_commit::Services*>(native.get()))s.d.provider->ReleasePreparedSource(*concrete);
  return Fail(e,"Native provider attempted an unregistered stopped root");
 }
 s.facade=s.cleanup->Prepare(std::move(native),p,e);if(!s.facade)return false;
 if(!s.typed_storage->ValidatePrepared(p,s.read_context,*s.cleanup,s.driver,
     static_cast<const transaction_root::Holder*>(s.holder),s.facade->Delegate()))
  return Fail(e,"Collected typed reference assignment proof refused");
 if(!s.stop->ContinueSource())return Fail(e,s.stop->Failure());
 return true;
}
std::unique_ptr<source_commit::Services> RunState::Finalize(const void*h,const source_commit::Plan**p)noexcept{
 auto&s=*state_;if(!p||s.phase!=State::Phase::Collected||h!=s.holder||!s.facade||!s.stop->IsFinalWindow(s.context.self))return nullptr;
 // Source publication ran after the Collected snapshot. Refresh the exact
 // owner's preallocated OS bounds in the actual original final window before
 // any registry/provider read. This explicit capture is the only OS boundary;
 // subsequent metadata/validation reads continue to use the owned context.
 if(!s.d.mapping.refresh_stopped(s.d.mapping.owner,nullptr)||!s.stop->IsFinalWindow(s.context.self)||!s.d.registry->Resolve())return nullptr;
 const auto r=s.d.registry->Final();if(!s.plan_roots.CanRefresh(r)||!s.ledger->CanRefresh(r)||!s.authority.CanRefresh(r)||!s.typed_storage||!s.typed_storage->CanRefresh(r)||(s.d.guards&&!s.d.guards->CanRefreshManagedRoots(r)))return nullptr;
 s.plan_roots.Refresh(r);s.ledger->Refresh(r);s.authority.Refresh(r);
 s.typed_storage->Refresh(r);
 if(!s.incoming.Refresh()||(s.d.guards&&!s.d.guards->RefreshManagedRoots(*s.plan,r))||!s.d.provider->RefreshManagedRoots(*s.plan,s.facade->Delegate(),r)||!s.cleanup->RefreshFinal(r))return nullptr;
 if(!s.typed_storage->Validate(*s.plan,s.read_context,*s.cleanup,s.driver,
     static_cast<const transaction_root::Holder*>(s.holder),s.facade->Delegate()))return nullptr;
 *p=s.plan;s.phase=State::Phase::Finalized;return std::move(s.facade);
}
void RunState::Commit(const source_commit::Plan&p)noexcept{auto&s=*state_;Require(s.phase==State::Phase::Finalized&&s.plan==&p&&s.cleanup->CommitObserver(p));s.hierarchy->BeginCommit(p);s.phase=State::Phase::Committed;}
void RunState::Refund()noexcept{
 auto&s=*state_;if(s.phase==State::Phase::Refunded)return;
 Require(s.phase!=State::Phase::Committed&&s.phase!=State::Phase::Ended);
 // A retained continuation cannot fail after original publication. Before
 // ContinueSource, rollback restores actual Runnable before private cleanup/JNI.
 Require(s.phase!=State::Phase::Collected&&s.phase!=State::Phase::Finalized);
 // Ledger and allocator stopped-root readers borrow cleanup, which in turn
 // borrows stop. Revoke the live stop before private cleanup/JNI, retain both
 // witnesses until all hierarchy refund readers have returned.
 if(s.stop)Require(s.stop->RollbackBeforeSource());
 // Direct-transition preparation failures retain a stopped reservation until
 // this rollback. Diagnose only after that owner restores actual Runnable.
 if(s.cleanup&&!s.cleanup->Frames().accepted&&s.cleanup->Frames().failure_thread)
  LogFrameReport(s.cleanup->Frames(),false);
 s.facade.reset();
 if(s.hierarchy&&s.hierarchy->Ready())s.hierarchy->Refund();
 // Session defers the callback while an original Finish/verifier view remains
 // open. Closing those views completes the same refund before original Run
 // can destroy Holder/DEX. An empty observer also needs this exact cleanup.
 if(!s.hierarchy)CompleteRefund(this);
}
void RunState::CompleteRefund(void*raw)noexcept{
 auto&run=*static_cast<RunState*>(raw);auto&s=*run.state_;
 Require(current==&run&&s.phase==State::Phase::Running);
 // All allocator/ledger/refund readers have finished. Preserve native anchor
 // links until original Run unwinds, but stop rooting the private temporary
 // Classes/caches beyond the original Holder/DEX lifetime.
 s.d.registry->Clear();Require(s.pool.DropManagedReferences());
 if(s.typed_storage)s.typed_storage->Clear(s.context.env);
 if(s.d.refund_roots)s.d.refund_roots(s.d.refund_roots_owner);
 s.RevokeFrameOwners();
 s.cleanup.reset();s.stop.reset();s.phase=State::Phase::Refunded;
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
struct RunState::SemanticAttempt {
 RunState& run;void* payload;native_hierarchy_cache::SemanticOperation operation;
};
bool RunState::InspectSemanticStopped(void* raw,native_source_stop::Session& stop)noexcept {
 auto& request=*static_cast<SemanticAttempt*>(raw);auto& r=request.run;auto&s=*r.state_;
 if(current!=&r||s.inspection||!s.mapper||!request.operation||!stop.IsStopped(s.context.self))return false;
 s.inspection=&stop;
 struct Reset {State& state;~Reset(){state.inspection=nullptr;}} reset{s};
 try {
  if(!s.d.mapping.refresh_stopped(s.d.mapping.owner,nullptr))return false;
  if(!s.mapper->EnterSemanticInspection(s.inspection_roots,s.inspection_context))return false;
  struct Leave {native_hierarchy_source_cache::Mapper& mapper;~Leave(){mapper.LeaveSemanticInspection();}} leave{*s.mapper};
  return request.operation(request.payload,*s.mapper);
 }catch(...){return false;}
}
bool RunState::InspectSemantic(void* raw,void* payload,native_hierarchy_cache::SemanticOperation operation)noexcept {
 auto*r=static_cast<RunState*>(raw);
 if(!r||current!=r||!operation)return false;auto&s=*r->state_;
 if(!s.mapper||s.inspection||!s.d.stop||!s.d.mapping.owner||!s.d.mapping.refresh_stopped||!s.d.mapping.extent)return false;
 SemanticAttempt request{*r,payload,operation};
 // A stopped source owner is reused. A continuing reservation is never a
 // license to suspend again or read a published semantic cache as Runnable.
 if(s.stop&&s.stop->IsStopped(s.context.self))return InspectSemanticStopped(&request,*s.stop);
 if((s.stop&&s.stop->Reserved())||(s.phase!=State::Phase::Constructed&&s.phase!=State::Phase::Running))return false;
 native_source_stop::Session temporary(*s.d.stop);
 return temporary.Inspect(s.context.self,s.extent,&request,InspectSemanticStopped);
}
bool RunState::MappingExtent(void*raw,const void*p,size_t*out)noexcept{auto*r=static_cast<RunState*>(raw);return r&&r==current&&r->StoppedWindow()&&r->state_->d.mapping.extent&&r->state_->d.mapping.extent(r->state_->d.mapping.owner,p,out);}
bool RunState::StoppedWindow()const noexcept{return current==this&&((state_->inspection&&state_->inspection->IsStopped(state_->context.self))||(state_->stop&&state_->stop->IsStopped(state_->context.self)));}
bool RunState::OwnsMapping(const void* owner)const noexcept{return current==this&&owner&&state_->d.mapping.owner==owner;}
bool RunState::VerifyHierarchy(const source_commit::Plan& p,size_t pair,void* loader)const noexcept {
 const auto& s=*state_;
 if(current!=this||s.plan!=&p||pair>=p.old_classes.size()||!s.stop||
    (s.phase!=State::Phase::Running&&s.phase!=State::Phase::Collected&&s.phase!=State::Phase::Finalized)||
    !s.stop->IsStopped(s.context.self)||!redefinition_gate::Owned(static_cast<JavaVM*>(s.context.vm)))return false;
 const auto* graph=s.authority.Staged();if(!graph)return false;
 size_t matches=0;for(const auto& node:graph->nodes)if(node.affected&&node.klass==p.old_classes[pair]&&node.loader==loader)++matches;
 if(matches!=1)return false;
 if(s.stop->IsFinalWindow(s.context.self))return s.authority.VerifyFinal(p,pair);
 return s.stop->IsCollectedStop(s.context.self)&&s.authority.VerifyPrepared(p,pair);
}
const modern_field_metadata::ReadContext* RunState::CurrentReadContext()const noexcept{return !StoppedWindow()?nullptr:state_->inspection?&state_->inspection_context:state_->read_context.extent?&state_->read_context:nullptr;}
const whole_process_frames::Report& RunState::Frames()const noexcept{Require(state_->cleanup!=nullptr);return state_->cleanup->Frames();}
}
