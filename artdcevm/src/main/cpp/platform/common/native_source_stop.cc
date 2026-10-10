#include "platform/common/native_source_stop.h"
#include "platform/common/native_source_class_loading.h"
#include "platform/common/native_cache_service.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
namespace artdcevm::native_source_stop {
namespace {
namespace a=abi::arm64::android_common;namespace s=a::source_stop;
thread_local Session* active=nullptr;
thread_local Session* finishing=nullptr;
thread_local Session* borrowed=nullptr;
thread_local Observation* observation=nullptr;
thread_local native_source_gc_publication::Scope* publication=nullptr;
std::mutex installation;bool installed=false;
abi::common::SuspendConstruct original_stop=nullptr;abi::common::SuspendDestroy original_resume=nullptr;
s::MutatorOperation original_disable=nullptr,original_enable=nullptr;
s::StartGC original_start=nullptr;s::FinishGC original_finish=nullptr;
using WaitGC=int32_t (*)(void*,int32_t,void*);
WaitGC original_wait=nullptr;
s::MutatorOperation original_mutex_lock=nullptr,original_mutex_unlock=nullptr;
bool waiting_installed=false,waiting_vendor_lock=false;
constexpr char kWait[]="_ZN3art2gc4Heap25WaitForGcToCompleteLockedENS0_7GcCauseEPNS_6ThreadE";
constexpr char kMutexLock[]="_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE";
constexpr char kMutexUnlock[]="_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE";
// Fixed thread_state.h: terminated=66, WaitingForGcToComplete=74, in both
// legacy16 and modern24. Only Runnable encoding changed; no state is written.
constexpr uint32_t kWaitingGC=74;
struct WaitingInvocation {
 const Service* service=nullptr;Session* session=nullptr;void* self=nullptr;
 size_t extent=0;void* latest_mutex=nullptr;void* frozen_mutex=nullptr;
 void* expected_mutex=nullptr;void* owner=nullptr;
 uint32_t latest_argument=0,frozen_argument=0,expected_argument=0;
 bool vendor_lock=false;
 bool (*prepare)(void*,Session&) noexcept=nullptr;
 unsigned waits=0,unlocks=0;bool inside_wait=false,wait_returned=false;
 bool valid=true,callback_returned=false,accepted=false,inside_callback=false,inspection=false;
};
thread_local WaitingInvocation* waiting_invocation=nullptr;

}
struct NativeHooks {
static bool WaitingState(const WaitingInvocation& w,uint32_t desired) noexcept {
 uint32_t state=UINT32_MAX;
 return w.service&&w.self&&w.service->current_()==w.self&&
   w.service->state_.State(w.self,w.extent,&state)&&state==desired;
}
static void VendorMutexReturned(void* mutex,void* self,uint32_t argument) noexcept {
 auto* w=waiting_invocation;
 if(w&&w->vendor_lock&&!w->inside_wait&&!w->wait_returned&&self==w->self){
  w->latest_mutex=mutex;w->latest_argument=argument;
 }
}
static void MutexLock(void* mutex,void* self) {
 original_mutex_lock(mutex,self);
 auto* w=waiting_invocation;
 if(w&&!w->vendor_lock&&!w->inside_wait&&!w->wait_returned&&self==w->self)w->latest_mutex=mutex;
}
static int32_t Wait(void* heap,int32_t cause,void* self) {
 auto* w=waiting_invocation;
 if(!w)return original_wait(heap,cause,self);
 if(w->inside_wait||w->waits||heap!=w->service->heap_||self!=w->self||
    cause!=s::kInstrumentationCause||!w->latest_mutex||!WaitingState(*w,kWaitingGC)){
  w->valid=false;return original_wait(heap,cause,self);
 }
 w->frozen_mutex=w->latest_mutex;w->frozen_argument=w->latest_argument;w->inside_wait=true;++w->waits;
 const int32_t result=original_wait(heap,cause,self);
 w->inside_wait=false;w->wait_returned=true;
 if(!WaitingState(*w,kWaitingGC))w->valid=false;
 return result; // kNone or preceding collection's actual result, unmodified.
}
static void MutexUnlock(void* mutex,void* self) {
 auto* w=waiting_invocation;
 const bool candidate=w&&w->wait_returned&&!w->inside_wait&&!w->inside_callback&&mutex==w->frozen_mutex&&self==w->self;
 original_mutex_unlock(mutex,self);
 if(!candidate)return;
 // Wait's ConditionVariable may release/reacquire this SAME mutex repeatedly.
 // Those unlocks happen inside_wait and cannot establish our boundary.
 if(++w->unlocks!=1||!w->valid||!WaitingState(*w,kWaitingGC)||
    (w->expected_mutex&&(w->expected_mutex!=mutex||(w->vendor_lock&&w->expected_argument!=w->frozen_argument)))){w->valid=false;return;}
 if(!w->session)return; // Ordinary full-return calibration only.
 auto& o=*w->session;w->inside_callback=true;
 o.gc_=true;o.waiting_window_=true;
 o.service_.stop_(&o.storage_,"artdcevm refundable source frame reservation",true);
 if(!WaitingState(*w,kWaitingGC))std::abort();
 o.suspended_=o.exclusive_=true;o.phase_=Session::Phase::kStopped;active=&o;
 const bool accepted=w->prepare(w->owner,o);w->callback_returned=true;
 if(w->inspection){
  if(o.phase_!=Session::Phase::kStopped||!o.IsCollectedStop(w->self))std::abort();
  w->accepted=accepted;
  if(!o.RollbackBeforeSource())std::abort();
 }else if(accepted){
  if(o.phase_!=Session::Phase::kContinuing||!o.Reserved()||o.exclusive_||!o.waiting_window_||
     !WaitingState(*w,kWaitingGC))std::abort();
  w->accepted=true;
 }else{
  if(o.phase_==Session::Phase::kContinuing)std::abort();
  if(!o.RollbackBeforeSource())std::abort();
 }
 o.waiting_window_=false;w->inside_callback=false;
 // No manual state transition: StartGC's original ScopedThreadStateChange
 // destructor now restores Runnable using ART checkpoints/flip/barrier logic.
}
static void PublicationEnter(native_source_gc_publication::Scope&) noexcept;
static void PublicationExit(native_source_gc_publication::Scope&) noexcept;
static bool PublicationStart(void*,void*,int32_t,int32_t) noexcept;
static bool PublicationFinish(void*,void*,int32_t) noexcept;
static void BindPublication() noexcept {
 using Scope=native_source_gc_publication::Scope;
 Scope::enter_=PublicationEnter;Scope::exit_=PublicationExit;
 Scope::start_=PublicationStart;Scope::finish_=PublicationFinish;
}
static void Construct(void* carrier,const char* cause,bool long_suspend) {
 if(Session::TransferConstruct(carrier,cause,long_suspend))return;
 if(!original_stop)std::abort();original_stop(carrier,cause,long_suspend);
 Session::OriginalConstructed(carrier,cause,long_suspend);
 Observation::Constructed(carrier,cause,long_suspend);
}
static void Destroy(void* carrier) {
 if(Session::TransferDestroy(carrier))return;
 Session::OriginalDestroying(carrier);
 if(!original_resume)std::abort();original_resume(carrier);
 Session::OriginalDestroyed(carrier);
 Observation::Destroyed(carrier);
}
static void Disable(void* heap,void* self){
 Session::SourceDisableBegin(heap,self);original_disable(heap,self);
 Session::OriginalDisabled(heap,self);
 Observation::DisableBegin(heap,self);
}
static void Enable(void* heap,void* self){
 Session::SourceDisableEnd(heap,self,false);original_enable(heap,self);Observation::DisableEnd(heap,self);
 Session::SourceDisableEnd(heap,self,true);
 Session::OriginalEnabled(heap,self);
}
static void Start(void* heap,void* self,int32_t cause,int32_t collector){
 if(native_source_gc_publication::Scope::Start(heap,self,cause,collector))return;
 Session::UnexpectedGC();Observation::GCOperation(heap,self,true);original_start(heap,self,cause,collector);
}
static void Finish(void* heap,void* self,int32_t type){
 if(native_source_gc_publication::Scope::Finish(heap,self,type))return;
 Session::UnexpectedGC();original_finish(heap,self,type);Observation::GCOperation(heap,self,false);
}
};
void NativeHooks::PublicationEnter(native_source_gc_publication::Scope& scope) noexcept {
 if(publication){if(active)std::abort();if(observation)observation->Refuse("source-publication-nested-callee");return;}
 if(active){auto& o=*active;size_t index=0;
  if(!o.service_.publication_calibrated_||!o.MatchPublication(scope.linker_,scope.cache_,scope.loader_,&index)||
     index>=o.publications_.size()||o.publications_[index])std::abort();
  scope.session_=&o;scope.index_=index;
 }else if(observation&&observation->phase_==Observation::Phase::kArmed){auto& o=*observation;
  if(!o.Ready()||!scope.linker_||!scope.cache_||!o.holder_||!o.State(o.service_.state_.RunnableState())||
     (o.publication_linker_&&o.publication_linker_!=scope.linker_)){
   o.Refuse("ordinary-source-publication-owner-mismatch");return;
  }
  scope.observation_=&o;
 }else return;
 scope.previous_=publication;publication=&scope;scope.attached_=true;
}
void NativeHooks::PublicationExit(native_source_gc_publication::Scope& scope) noexcept {
 if(!scope.attached_)return;if(publication!=&scope)std::abort();
 const bool pair=(scope.starts_==0&&scope.finishes_==0)||(scope.starts_==1&&scope.finishes_==1);
 if(scope.session_){auto& o=*scope.session_;size_t index=0;
  if(!scope.returned_||!pair||scope.starts_!=o.service_.publication_gc_pairs_||
     (scope.starts_&&scope.collector_!=o.service_.publication_collector_)||
     !o.MatchPublication(scope.linker_,scope.cache_,scope.loader_,&index)||index!=scope.index_||
     index>=o.publications_.size()||o.publications_[index])std::abort();
  o.publications_[index]=1;++o.publication_completed_;
 }else if(scope.observation_){auto& o=*scope.observation_;
  if(!scope.returned_||!pair||observation!=&o||!o.Ready()||o.phase_!=Observation::Phase::kArmed||
     !o.State(o.service_.state_.RunnableState())||o.prefinal_gc_||
     (o.publication_profile_&&(o.publication_pairs_!=scope.starts_||o.publication_collector_!=scope.collector_)))
   o.Refuse("ordinary-source-publication-GC-profile-mismatch");
  else{o.publication_linker_=scope.linker_;o.publication_pairs_=scope.starts_;o.publication_collector_=scope.collector_;o.publication_profile_=true;++o.publications_;}
 }
 publication=scope.previous_;scope.attached_=false;
}
bool NativeHooks::PublicationStart(void* heap,void* self,int32_t cause,int32_t collector) noexcept {
 if(!publication)return false;auto& p=*publication;
 const bool shape=p.starts_==0&&p.finishes_==0&&cause==12&&(collector==11||collector==13);
 if(p.session_){auto& o=*p.session_;
  if(!shape||active!=&o||heap!=o.service_.heap_||self!=o.self_||!o.IsPublicationWindow(self)||
     o.service_.publication_gc_pairs_!=1||collector!=o.service_.publication_collector_)std::abort();
  p.starts_=1;p.collector_=collector;return true;
 }
 if(p.observation_){auto& o=*p.observation_;
  if(!shape||observation!=&o||heap!=o.service_.heap_||self!=o.self_||!o.State(o.service_.state_.RunnableState()))
   o.Refuse("ordinary-source-publication-GC-start-mismatch");
  else{p.starts_=1;p.collector_=collector;}
 }
 return false;
}
bool NativeHooks::PublicationFinish(void* heap,void* self,int32_t type) noexcept {
 if(!publication)return false;auto& p=*publication;
 const bool shape=p.starts_==1&&p.finishes_==0&&type==s::kNoCollection;
 if(p.session_){auto& o=*p.session_;
  if(!shape||active!=&o||heap!=o.service_.heap_||self!=o.self_||!o.IsPublicationWindow(self))std::abort();
  p.finishes_=1;return true;
 }
 if(p.observation_){auto& o=*p.observation_;
  if(!shape||observation!=&o||heap!=o.service_.heap_||self!=o.self_||!o.State(o.service_.state_.RunnableState()))
   o.Refuse("ordinary-source-publication-GC-finish-mismatch");
  else p.finishes_=1;
 }
 return false;
}
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
}
bool Install(HookEngine& hooks,std::string* error) {
 std::lock_guard<std::mutex> guard(installation);if(installed)return true;
 auto ctor=hooks.FindSymbol("libart.so",abi::common::kSuspendConstruct);
 auto dtor=hooks.FindSymbol("libart.so",abi::common::kSuspendDestroy);
 // Empty carrier C1/C2 and D1/D2 must share the actual named implementation;
 // no caller-PC inference or arbitrary source final window is accepted.
 if(!ctor||!dtor||hooks.FindSymbol("libart.so","_ZN3art16ScopedSuspendAllC1EPKcb")!=ctor||
    hooks.FindSymbol("libart.so","_ZN3art16ScopedSuspendAllD1Ev")!=dtor)
   return Fail(error,"Native source stop requires shared named complete/base scope bodies");
 struct Binding{void* target;void* proxy;void** original;};
 Binding bindings[]{
  {ctor,reinterpret_cast<void*>(NativeHooks::Construct),reinterpret_cast<void**>(&original_stop)},
  {dtor,reinterpret_cast<void*>(NativeHooks::Destroy),reinterpret_cast<void**>(&original_resume)},
  {hooks.FindSymbol("libart.so",s::kDisableMoving),reinterpret_cast<void*>(NativeHooks::Disable),reinterpret_cast<void**>(&original_disable)},
  {hooks.FindSymbol("libart.so",s::kEnableMoving),reinterpret_cast<void*>(NativeHooks::Enable),reinterpret_cast<void**>(&original_enable)},
  {hooks.FindSymbol("libart.so",s::kStartGC),reinterpret_cast<void*>(NativeHooks::Start),reinterpret_cast<void**>(&original_start)},
  {hooks.FindSymbol("libart.so",s::kFinishGC),reinterpret_cast<void*>(NativeHooks::Finish),reinterpret_cast<void**>(&original_finish)}};
 for(size_t i=0;i<6;++i){
  if(!bindings[i].target)return Fail(error,"Source observer lacks original heap disable/GC leaves");
  for(size_t j=0;j<i;++j)if(bindings[j].target==bindings[i].target)
    return Fail(error,"Source observer distinct native operations share a target");
 }
 abi::common::SuspendAll scope{};reinterpret_cast<abi::common::SuspendConstruct>(ctor)(&scope,"artdcevm source stop installation",false);
 void* tokens[6]{};size_t count=0;
 for(auto& binding:bindings){
  auto token=hooks.Install(binding.target,binding.proxy,binding.original,error);
  if(!token)break;if(!*binding.original)std::abort();tokens[count++]=token;
 }
 installed=count==6;
 if(!installed){std::string rollback;while(count)if(!hooks.Remove(tokens[--count],&rollback))std::abort();
  for(auto& binding:bindings)*binding.original=nullptr;}
 reinterpret_cast<abi::common::SuspendDestroy>(dtor)(&scope);
 if(installed)NativeHooks::BindPublication();
 return installed;
}

bool InstallOriginalWaitingScope(HookEngine& hooks,std::string* error) {
 std::lock_guard<std::mutex> guard(installation);if(waiting_installed)return true;
 if(!installed)return Fail(error,"Original WaitingGC bridge requires shared source observer");
 struct Binding{void* target;void* proxy;void** original;};
 Binding b[]{
  {hooks.FindSymbol("libart.so",kWait),reinterpret_cast<void*>(NativeHooks::Wait),reinterpret_cast<void**>(&original_wait)},
  {hooks.FindSymbol("libart.so",kMutexLock),reinterpret_cast<void*>(NativeHooks::MutexLock),reinterpret_cast<void**>(&original_mutex_lock)},
  {hooks.FindSymbol("libart.so",kMutexUnlock),reinterpret_cast<void*>(NativeHooks::MutexUnlock),reinterpret_cast<void**>(&original_mutex_unlock)}};
 const bool vendor=!b[1].target;
 if(!b[0].target||!b[2].target||b[0].target==b[2].target)
  return Fail(error,"Original WaitingGC bridge lacks distinct named wait/unlock leaves");
 if(vendor){
  auto vendor_target=hooks.FindSymbol("libart.so","_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadEj");
  if(!vendor_target||vendor_target==b[0].target||vendor_target==b[2].target)
   return Fail(error,"Original WaitingGC bridge lacks a supported named mutex lock");
  // The cache service owns this overload already. It keeps its unique hook and
  // original argument capture; we subscribe solely to normal original returns.
  if(!native_cache::ObserveVendorMutexReturns(hooks,NativeHooks::VendorMutexReturned,error))return false;
 }
 for(size_t i=0;i<3;++i){if(vendor&&i==1)continue;if(!b[i].target)return Fail(error,"Original WaitingGC bridge lacks canonical named mutex/wait leaves");
  for(size_t j=0;j<i;++j)if(b[j].target&&b[i].target==b[j].target)return Fail(error,"Original WaitingGC bridge aliases distinct leaves");}
 abi::common::SuspendAll storage{};original_stop(&storage,"artdcevm original WaitingGC bridge installation",false);
 void* tokens[3]{};size_t n=0;
 for(auto& x:b){if(!x.target)continue;auto token=hooks.Install(x.target,x.proxy,x.original,error);if(!token)break;
  if(!*x.original)std::abort();tokens[n++]=token;}
 waiting_installed=n==(vendor?2u:3u);waiting_vendor_lock=waiting_installed&&vendor;
 if(!waiting_installed){std::string rollback;while(n)if(!hooks.Remove(tokens[--n],&rollback))std::abort();
  for(auto& x:b)*x.original=nullptr;}
 original_resume(&storage);return waiting_installed;
}
bool Service::CalibrateOriginalWaiting(void* self,size_t extent,std::string* error) {
 waiting_calibrated_=false;waiting_mutex_=nullptr;
 if(!waiting_installed||active||finishing||borrowed||observation||waiting_invocation||!self||!heap_||
    !mutator_||*mutator_!=mutator_identity_||!current_||current_()!=self)
  return Fail(error,"Original WaitingGC calibration lacks independent ordinary receiver");
 WaitingInvocation w;w.service=this;w.self=self;w.extent=extent;w.vendor_lock=waiting_vendor_lock;
 if(!NativeHooks::WaitingState(w,state_.RunnableState()))return Fail(error,"Original WaitingGC calibration requires actual Runnable owner");
 waiting_invocation=&w;
 original_start(heap_,self,s::kInstrumentationCause,s::kNonMovingReservation);
 waiting_invocation=nullptr;
 // Even a denied shape has actually called StartGC. Always release its real
 // reservation before any return; never create a synthetic failed-ready state.
 const bool complete=w.valid&&w.waits==1&&w.unlocks==1&&w.wait_returned&&w.frozen_mutex&&
   NativeHooks::WaitingState(w,state_.RunnableState());
 original_finish(heap_,self,s::kNoCollection);
 if(!complete||!NativeHooks::WaitingState(w,state_.RunnableState()))
  return Fail(error,"Original WaitingGC full return/mutex identity proof failed");
 waiting_mutex_=w.frozen_mutex;waiting_argument_=w.frozen_argument;waiting_calibrated_=true;return true;
}

bool Service::Bind(HookEngine& hooks,void* heap,const thread_suspend_service::Service& state,Service* out,std::string* error) {
 if(!out||!heap||!installed)return Fail(error,"Source observer lacks prepared receiver/installed transfer");
 Service c;c.heap_=heap;c.state_=state;
#define ART(member,name) c.member=reinterpret_cast<decltype(c.member)>(hooks.FindSymbol("libart.so",name))
 ART(mutator_,s::kMutator);ART(current_,s::kCurrent);ART(start_,s::kStartGC);ART(finish_,s::kFinishGC);
 ART(lock_,s::kLock);ART(unlock_,s::kUnlock);ART(stop_,abi::common::kSuspendConstruct);ART(resume_,abi::common::kSuspendDestroy);
#undef ART
 if(!c.mutator_||!*c.mutator_||!c.current_||!c.start_||!c.finish_||!c.lock_||!c.unlock_||!c.stop_||!c.resume_)
   return Fail(error,"Source stop lacks exact native heap/mutator/scope leaves");
 c.mutator_identity_=*c.mutator_;*out=c;return true;
}
bool Session::SameOwner()const noexcept{return self_&&service_.current_&&service_.current_()==self_&&
 service_.mutator_&&*service_.mutator_==service_.mutator_identity_;}
bool Session::Native()const noexcept {
 uint32_t state=UINT32_MAX;return SameOwner()&&service_.state_.State(self_,extent_,&state)&&state==abi::arm64::android11::kNative;
}
bool Session::StoppedState()const noexcept {
 if(!waiting_window_)return Native();
 uint32_t state=UINT32_MAX;
 return waiting_invocation&&waiting_invocation->session==this&&SameOwner()&&
   service_.state_.State(self_,extent_,&state)&&state==kWaitingGC;
}
bool Session::BeginOriginalWaiting(void* self,size_t extent,void* owner,
    bool (*prepare)(void*,Session&) noexcept) noexcept {
 return RunOriginalWaiting(self,extent,owner,prepare,false);
}
bool Session::InspectOriginalWaiting(void* self,size_t extent,void* owner,
    bool (*inspect)(void*,Session&) noexcept) noexcept {
 return RunOriginalWaiting(self,extent,owner,inspect,true);
}
bool Session::RunOriginalWaiting(void* self,size_t extent,void* owner,
    bool (*prepare)(void*,Session&) noexcept,bool inspection) noexcept {
 if(phase_!=Phase::kEmpty||active||finishing||borrowed||observation||waiting_invocation||
    !service_.calibrated_||!service_.waiting_calibrated_||!owner||!prepare||!self||
    service_.current_()!=self||!service_.mutator_||*service_.mutator_!=service_.mutator_identity_){
  failure_="original-WaitingGC-owner-or-lifecycle-mismatch";return false;
 }
 self_=self;extent_=extent;
 WaitingInvocation w;w.service=&service_;w.session=this;w.self=self;w.extent=extent;
 w.vendor_lock=waiting_vendor_lock;w.expected_argument=service_.waiting_argument_;
 w.expected_mutex=service_.waiting_mutex_;w.owner=owner;w.prepare=prepare;w.inspection=inspection;
 if(!NativeHooks::WaitingState(w,service_.state_.RunnableState())){
  failure_="original-WaitingGC-requires-actual-Runnable";return false;
 }
 waiting_invocation=&w;
 original_start(service_.heap_,self_,s::kInstrumentationCause,s::kNonMovingReservation);
 waiting_invocation=nullptr;
 if(!NativeHooks::WaitingState(w,service_.state_.RunnableState()))std::abort();
 if(!w.callback_returned){original_finish(service_.heap_,self_,s::kNoCollection);
  phase_=Phase::kRolledBack;failure_="original-WaitingGC-calibrated-shape-mismatch";return false;}
 if(!w.valid||w.waits!=1||w.unlocks!=1||w.frozen_mutex!=service_.waiting_mutex_||(w.vendor_lock&&w.frozen_argument!=service_.waiting_argument_))std::abort();
 if(inspection){
  if(Reserved()||active==this||phase_!=Phase::kRolledBack)std::abort();
  failure_=w.accepted?"":"original-WaitingGC-inspection-refused";return w.accepted;
 }
 if(w.accepted){if(phase_!=Phase::kContinuing||!IsPublicationWindow(self_))std::abort();return true;}
 if(Reserved()||active==this||phase_!=Phase::kRolledBack)std::abort();
 failure_="original-WaitingGC-stopped-plan-refused";return false;
}
bool Session::PrepareDexPublication(void* driver,const void* holder,void* linker,size_t count,
    native_source_gc_publication::Matcher matcher,std::string* error){
 if(phase_!=Phase::kEmpty||!publications_.empty()||!service_.publication_calibrated_||
    !driver||!holder||!linker||linker!=service_.publication_linker_||!count||count>1000000||
    !matcher.owner||!matcher.match)return Fail(error,"Source publication requires complete ordinary staged owner and actual named profile");
 try{publications_.assign(count,0);}catch(const std::bad_alloc&){return Fail(error,"Source publication bitmap allocation failed");}
 publication_driver_=driver;publication_holder_=holder;publication_linker_=linker;publication_matcher_=matcher;
 return true;
}
bool Session::IsPublicationWindow(void* self)const noexcept {
 uint32_t state=UINT32_MAX;
 return self==self_&&active==this&&phase_==Phase::kContinuing&&gc_&&suspended_&&!exclusive_&&!source_disable_&&
  SameOwner()&&service_.state_.State(self_,extent_,&state)&&state==service_.state_.RunnableState();
}
bool Session::CanPublishDex(void* driver,const void* holder,void* linker)const noexcept {
 return IsPublicationWindow(self_)&&publication_driver_==driver&&publication_holder_==holder&&
  publication_linker_==linker&&!publications_.empty()&&publication_matcher_.owner&&publication_matcher_.match;
}
bool Session::MatchPublication(void* linker,void* cache,void* loader,size_t* index)const noexcept {
 return cache&&index&&CanPublishDex(publication_driver_,publication_holder_,linker)&&
  publication_matcher_.match(publication_matcher_.owner,publication_driver_,publication_holder_,linker,cache,loader,index);
}
bool Session::Begin(void* self,size_t extent) noexcept {
 if(phase_!=Phase::kEmpty||active||finishing||borrowed||observation||!service_.calibrated_||!service_.state_.CanTransitionRunnable()||!service_.heap_||!service_.mutator_||*service_.mutator_!=service_.mutator_identity_||!self||service_.current_()!=self){failure_="source-stop-owner-or-lifecycle-mismatch";return false;}
 self_=self;extent_=extent;uint32_t state=UINT32_MAX;
 if(!service_.state_.State(self_,extent_,&state)||state!=service_.state_.RunnableState()){failure_="source-stop-requires-calibrated-Runnable-owner";return false;}
 // StartGC really waits for the preceding owner, then excludes every GC until
 // matching FinishGC. MS here is only an audited nonmoving exclusion sentinel;
 // no collector runs and FinishGC(kNone) increments no collection statistics.
 bool transitioned=false;
 if(!service_.state_.EnterNative(self_,extent_,&transitioned)||!transitioned||!Native()){
  failure_="source-stop-native-transition-failed";return false;
 }
 // Enter GC exclusion from Native. Old StartGC's state-change destructor may
 // otherwise wait to restore Runnable while already owning the GC reservation.
 // All managed plans must refresh their pre-registered native strong roots after
 // this wait and before use; the scanner itself uses only native frame storage.
 service_.start_(service_.heap_,self_,s::kInstrumentationCause,s::kNonMovingReservation);gc_=true;
 if(!Native())std::abort();
 service_.stop_(&storage_,"artdcevm refundable source frame reservation",true);
 suspended_=exclusive_=true;phase_=Phase::kStopped;active=this;failure_="";return true;
}
bool Session::IsStopped(void* self)const noexcept {
 if(borrowed_)return self==self_&&borrowed==this&&!borrowed_failed_&&source_disable_&&
   original_holder_&&source_carrier_&&suspended_&&exclusive_&&phase_==Phase::kFinal&&Native();
 return self==self_&&active==this&&gc_&&suspended_&&exclusive_&&
   (phase_==Phase::kStopped||phase_==Phase::kFinal)&&StoppedState();
}
bool Session::IsCollectedStop(void* self)const noexcept {return phase_==Phase::kStopped&&IsStopped(self);}
bool Session::IsFinalWindow(void* self)const noexcept {return phase_==Phase::kFinal&&IsStopped(self);}
bool Session::ContinueSource() noexcept {
 if(service_.publication_calibrated_&&publications_.empty()){failure_="source-stop-missing-complete-publication-owner";return false;}
 if(!IsStopped(self_)||phase_!=Phase::kStopped){failure_="source-stop-continuation-owner-mismatch";return false;}
 service_.unlock_(*service_.mutator_,self_);exclusive_=false;
 if(waiting_window_){phase_=Phase::kContinuing;return true;}
 if(!service_.state_.RestoreRunnable(self_))std::abort();
 uint32_t state=UINT32_MAX;if(!SameOwner()||!service_.state_.State(self_,extent_,&state)||state!=service_.state_.RunnableState())std::abort();
 phase_=Phase::kContinuing;return true;
}
bool Session::TransferConstruct(void* carrier,const char* cause,bool long_suspend) noexcept {
 if(!active)return false;auto& owner=*active;
 // A constructor in the stopped scan is unrelated to the transfer and would
 // nest global suspension. It is invalid, not an invitation to run the leaf.
 if(owner.phase_!=Phase::kContinuing||!carrier||!cause||std::strcmp(cause,owner.service_.final_cause_)||long_suspend!=owner.service_.final_long_||!owner.source_disable_||!owner.Native()||!owner.Reserved()||owner.exclusive_)std::abort();
 owner.service_.lock_(*owner.service_.mutator_,owner.self_);owner.exclusive_=true;
 owner.source_carrier_=carrier;owner.phase_=Phase::kFinal;return true;
}
bool Session::TransferDestroy(void* carrier) noexcept {
 if(!active||carrier!=active->source_carrier_)return false;
 auto& owner=*active;if(owner.phase_!=Phase::kFinal||!owner.IsStopped(owner.self_))std::abort();
 // The empty original source carrier owns the exact same native reservation.
 // Resume uses its original destructor; no invented suspend count is written.
 active=nullptr;owner.service_.resume_(&owner.storage_);owner.suspended_=owner.exclusive_=false;
 owner.service_.finish_(owner.service_.heap_,owner.self_,s::kNoCollection);owner.gc_=false;
 owner.phase_=Phase::kFinalResumed;finishing=&owner;return true;
}
bool Session::RollbackBeforeSource() noexcept {
 if(borrowed_||(phase_!=Phase::kEmpty&&phase_!=Phase::kStopped&&phase_!=Phase::kRolledBack))return false;
 Rollback();return !Reserved()&&(phase_==Phase::kEmpty||phase_==Phase::kRolledBack);
}
void Session::Rollback() noexcept {
 if(borrowed_){
  if(phase_==Phase::kFinal||phase_==Phase::kOriginalClosing)std::abort();
  if(borrowed==this)borrowed=nullptr;
  return; // Never destroy a carrier or restore a transition owned by original ART.
 }
 if(phase_==Phase::kEmpty||phase_==Phase::kCompleted||phase_==Phase::kRolledBack)return;
 if(!SameOwner()||phase_==Phase::kFinal||phase_==Phase::kFinalResumed)std::abort();
 if(phase_==Phase::kContinuing){bool moved=false;if(!service_.state_.EnterNative(self_,extent_,&moved)||!moved||!Native())std::abort();service_.lock_(*service_.mutator_,self_);exclusive_=true;}
 if(!StoppedState()||!exclusive_||!Reserved())std::abort();
 active=nullptr;service_.resume_(&storage_);suspended_=exclusive_=false;
 service_.finish_(service_.heap_,self_,s::kNoCollection);gc_=false;
 if(!waiting_window_&&!service_.state_.RestoreRunnable(self_))std::abort();phase_=Phase::kRolledBack;
}
void Session::SourceDisableBegin(void* heap,void* self) noexcept {
 if(!active)return;auto& owner=*active;uint32_t state=UINT32_MAX;
 if(publication||(!owner.publications_.empty()&&owner.publication_completed_!=owner.publications_.size())||owner.phase_!=Phase::kContinuing||owner.source_disable_||heap!=owner.service_.heap_||self!=owner.self_||
    !owner.SameOwner()||!owner.service_.state_.State(self,owner.extent_,&state)||state!=owner.service_.state_.RunnableState())std::abort();
 owner.source_disable_=true;
}
void Session::SourceDisableEnd(void* heap,void* self,bool completed) noexcept {
 if(!finishing){if(active)std::abort();return;}auto& owner=*finishing;uint32_t state=UINT32_MAX;
 if(owner.phase_!=Phase::kFinalResumed||!owner.source_disable_||heap!=owner.service_.heap_||self!=owner.self_||
    !owner.SameOwner()||!owner.service_.state_.State(self,owner.extent_,&state)||state!=owner.service_.state_.RunnableState())std::abort();
 if(completed){owner.source_disable_=false;owner.phase_=Phase::kCompleted;finishing=nullptr;}
}
void Session::UnexpectedGC() noexcept {
 if(active)std::abort();
 if(borrowed&&(borrowed->phase_==Phase::kFinal||borrowed->phase_==Phase::kOriginalDisabled))
  borrowed->RefuseOriginal("original-final-unexpected-GC");
}
bool Service::SameBinding(const Service& other)const noexcept {
 return state_.SameBinding(other.state_)&&heap_==other.heap_&&mutator_identity_==other.mutator_identity_&&mutator_&&*mutator_==mutator_identity_&&mutator_==other.mutator_&&current_==other.current_&&start_==other.start_&&finish_==other.finish_&&
   lock_==other.lock_&&unlock_==other.unlock_&&stop_==other.stop_&&resume_==other.resume_;
}
bool Service::Authorize(const Calibration& proof,const void* run) noexcept {
 if(!proof.valid_||!run||run!=proof.run_body_||!SameBinding(proof.binding_)||(!state_.CanTransitionRunnable()&&!waiting_calibrated_))return false;
 std::memcpy(final_cause_,proof.cause_,sizeof(final_cause_));final_long_=proof.long_;calibrated_=true;
 publication_calibrated_=proof.publication_valid_;publication_linker_=proof.publication_linker_;
 publication_gc_pairs_=proof.publication_pairs_;publication_collector_=proof.publication_collector_;return true;
}
bool Service::AuthorizeOriginalFinal(const Calibration& proof,const void* run) noexcept {
 original_calibrated_=false;original_run_=nullptr;
 if(!proof.valid_||!run||run!=proof.run_body_||!SameBinding(proof.binding_))return false;
 std::memcpy(final_cause_,proof.cause_,sizeof(final_cause_));final_long_=proof.long_;
 original_run_=run;original_calibrated_=true;return true;
}
void Session::RefuseOriginal(const char* failure) noexcept {
 if(!borrowed_failed_)failure_=failure;
 borrowed_failed_=true;
}
bool Session::PrepareOriginalFinal(void* self,size_t extent,const void* run,void* driver) noexcept {
 if(phase_!=Phase::kEmpty||active||finishing||borrowed||!service_.original_calibrated_||
    !run||run!=service_.original_run_||!driver||!self||service_.current_()!=self)return false;
 self_=self;extent_=extent;uint32_t state=UINT32_MAX;
 if(!SameOwner()||!service_.state_.State(self,extent,&state)||state!=service_.state_.RunnableState())return false;
 borrowed_=true;original_driver_=driver;phase_=Phase::kOriginalPrepared;borrowed=this;
 failure_="original-final-not-open";return true;
}
bool Session::OriginalCollected(void* driver,const void* holder) noexcept {
 uint32_t state=UINT32_MAX;
 if(!borrowed_||borrowed!=this||borrowed_failed_||phase_!=Phase::kOriginalPrepared||
    driver!=original_driver_||!holder||!SameOwner()||
    !service_.state_.State(self_,extent_,&state)||state!=service_.state_.RunnableState()){
  if(borrowed_)RefuseOriginal("original-final-collected-owner-mismatch");return false;
 }
 original_holder_=holder;phase_=Phase::kOriginalArmed;return true;
}
void Session::OriginalDisabled(void* heap,void* self) noexcept {
 if(!borrowed)return;auto& o=*borrowed;uint32_t state=UINT32_MAX;
 if(o.phase_==Phase::kOriginalPrepared)return; // Earlier original allocation work.
 if(o.borrowed_failed_||o.phase_!=Phase::kOriginalArmed||heap!=o.service_.heap_||self!=o.self_||
    !o.SameOwner()||!o.service_.state_.State(self,o.extent_,&state)||state!=o.service_.state_.RunnableState()){
  o.RefuseOriginal("original-final-disable-owner-mismatch");return;
 }
 o.source_disable_=true;o.phase_=Phase::kOriginalDisabled;
}
void Session::OriginalConstructed(void* carrier,const char* cause,bool long_suspend) noexcept {
 if(!borrowed)return;auto& o=*borrowed;
 if(o.phase_==Phase::kOriginalPrepared)return; // Original class-loading initialization.
 if(o.borrowed_failed_||o.phase_!=Phase::kOriginalDisabled||!carrier||!cause||
    std::strcmp(cause,o.service_.final_cause_)||long_suspend!=o.service_.final_long_||
    !o.source_disable_||!o.original_holder_||!o.Native()){
  o.RefuseOriginal("original-final-carrier-owner-mismatch");return;
 }
 // Only reached after the genuine named constructor returned normally. Its
 // fixed ART implementation owns exclusive mutator access and global suspension.
 o.source_carrier_=carrier;o.suspended_=o.exclusive_=true;o.phase_=Phase::kFinal;o.failure_="";
}
void Session::OriginalDestroying(void* carrier) noexcept {
 if(!borrowed)return;auto& o=*borrowed;
 if(o.phase_!=Phase::kFinal)return;
 if(carrier!=o.source_carrier_||!o.Native())o.RefuseOriginal("original-final-resume-owner-mismatch");
 o.exclusive_=false;o.phase_=Phase::kOriginalClosing; // Revoke before original resumes threads.
}
void Session::OriginalDestroyed(void* carrier) noexcept {
 if(!borrowed)return;auto& o=*borrowed;
 if(o.phase_!=Phase::kOriginalClosing)return;
 if(carrier!=o.source_carrier_||!o.Native())o.RefuseOriginal("original-final-return-owner-mismatch");
 o.suspended_=false;o.source_carrier_=nullptr;o.phase_=Phase::kFinalResumed;
}
void Session::OriginalEnabled(void* heap,void* self) noexcept {
 if(!borrowed)return;auto& o=*borrowed;uint32_t state=UINT32_MAX;
 if(o.phase_==Phase::kOriginalPrepared)return;
 if(o.phase_!=Phase::kFinalResumed||heap!=o.service_.heap_||self!=o.self_||!o.SameOwner()||
    !o.service_.state_.State(self,o.extent_,&state)||state!=o.service_.state_.RunnableState()){
  o.RefuseOriginal("original-final-enable-owner-mismatch");return;
 }
 o.source_disable_=false;o.phase_=o.borrowed_failed_?Phase::kRolledBack:Phase::kCompleted;
 borrowed=nullptr;
}
void Observation::Refuse(const char* failure) noexcept {
 if(!failed_){failure_=failure;trace_.first_failure_phase=static_cast<uint8_t>(phase_);}
 failed_=true;trace_.failed=true;
}
void Observation::Event(uint8_t event) noexcept {
 trace_.events.Record(event);const size_t index=event-1;
 if(trace_.counts[index]!=UINT32_MAX)++trace_.counts[index];
 trace_.phase=static_cast<uint8_t>(phase_);trace_.phase_masks[index]|=uint32_t{1}<<trace_.phase;
 uint32_t state=UINT32_MAX;
 if(!service_.current_||service_.current_()!=self_){trace_.current_mismatch|=uint32_t{1}<<index;trace_.state_unreadable|=uint32_t{1}<<index;}
 else if(!service_.state_.State(self_,extent_,&state))trace_.state_unreadable|=uint32_t{1}<<index;
 trace_.states[index]=state;
}
bool Observation::State(uint32_t expected)const noexcept {
 uint32_t value=UINT32_MAX;return service_.current_&&service_.current_()==self_&&service_.state_.State(self_,extent_,&value)&&value==expected;
}
Observation::Observation(const Service& service,void* driver,const void* run,void* self,size_t extent,native_source_class_loading::Run* cleanup) noexcept:
 service_(service),driver_(driver),run_body_(run),self_(self),extent_(extent),cleanup_(cleanup){
 trace_.expected_runnable=service_.state_.RunnableState();Event(1);
 if(observation||active||finishing||!driver||!run||!service_.heap_||!State(service_.state_.RunnableState())){Refuse("ordinary-source-observation-owner-mismatch");return;}
 observation=this;attached_=true;failure_="";
}
Observation::~Observation(){if(attached_){if(observation!=this)std::abort();observation=nullptr;}}
bool Observation::Ready()const noexcept{return attached_&&!failed_&&!finished_&&observation==this;}
bool Observation::BindCleanup(native_source_class_loading::Run& cleanup)noexcept{
 if(!Ready()||phase_!=Phase::kRun||cleanup_||!cleanup.OwnsDriver(driver_)){Refuse("ordinary-source-cleanup-Run-binding-mismatch");return false;}
 cleanup_=&cleanup;return true;
}
bool Observation::Collected(void* driver,const void* holder) noexcept {
 Event(2);trace_.collected_ready=Ready();trace_.collected_driver_match=driver==driver_;trace_.collected_holder_present=holder!=nullptr;
 if(!Ready()||phase_!=Phase::kRun||driver!=driver_||!holder||!State(service_.state_.RunnableState())){
  Refuse("ordinary-source-Collected-owner-order-mismatch");return false;}
 holder_=holder;phase_=Phase::kArmed;return true;
}
void Observation::DisableBegin(void* heap,void* self) noexcept {
 if(!observation)return;auto& o=*observation;o.Event(3);if(o.phase_==Phase::kRun)return;
 if(!o.Ready()||o.phase_!=Phase::kArmed||o.prefinal_gc_||heap!=o.service_.heap_||self!=o.self_||!o.State(o.service_.state_.RunnableState())){
  o.Refuse("ordinary-source-original-GC-disable-owner-order-mismatch");return;}
 o.phase_=Phase::kDisabled;
}
void Observation::Constructed(void* carrier,const char* cause,bool long_suspend) noexcept {
 if(!observation)return;auto& o=*observation;o.Event(4);if(o.phase_==Phase::kRun)return;
 if(o.Ready()&&o.phase_==Phase::kComplete&&o.cleanup_&&o.cleanup_->OwnsDriver(o.driver_)&&
    !o.postfinal_gc_&&o.State(abi::arm64::android11::kNative)&&o.cleanup_->BeginCleanup(carrier)){
  o.carrier_=carrier;o.phase_=Phase::kCleanupStopped;return;
 }
 if(!o.Ready()||o.phase_!=Phase::kDisabled||!carrier||!cause||!o.State(abi::arm64::android11::kNative)){
  o.Refuse("ordinary-source-original-final-stop-owner-order-mismatch");return;}
 size_t length=0;while(length<sizeof(o.cause_)&&cause[length])++length;
 if(!length||length==sizeof(o.cause_)){o.Refuse("ordinary-source-original-final-stop-cause-unbounded");return;}
 std::memcpy(o.cause_,cause,length+1);o.long_=long_suspend;o.carrier_=carrier;o.phase_=Phase::kStopped;
}
void Observation::Destroyed(void* carrier) noexcept {
 if(!observation)return;auto& o=*observation;o.Event(5);if(o.phase_==Phase::kRun)return;
 if(o.Ready()&&o.phase_==Phase::kCleanupStopped&&o.cleanup_&&carrier==o.carrier_&&
    o.State(abi::arm64::android11::kNative)&&o.cleanup_->EndCleanup(carrier)){
  o.phase_=Phase::kCleanupResumed;return;
 }
 if(!o.Ready()||o.phase_!=Phase::kStopped||carrier!=o.carrier_||!o.State(abi::arm64::android11::kNative)){
  o.Refuse("ordinary-source-original-final-resume-owner-order-mismatch");return;}
 o.phase_=Phase::kResumed;
}
void Observation::DisableEnd(void* heap,void* self) noexcept {
 if(!observation)return;auto& o=*observation;o.Event(6);if(o.phase_==Phase::kRun)return;
 if(!o.Ready()||o.phase_!=Phase::kResumed||heap!=o.service_.heap_||self!=o.self_||!o.State(o.service_.state_.RunnableState())){
  o.Refuse("ordinary-source-original-GC-enable-owner-order-mismatch");return;}
 o.phase_=Phase::kComplete;
}
void Observation::GCOperation(void* heap,void* self,bool starting) noexcept {
 if(!observation)return;auto& o=*observation;o.Event(7);
 if(o.phase_==Phase::kRun)return;
 // Collection precedes cache registration and breakpoint removal. GC is still
 // permitted there: this observer retains only the native Holder identity, no
 // borrowed managed pointer. A same-thread heap pair must finish BEFORE the
 // actual original moving-GC disable returns and arms the final interval.
 // AOSP legacy Reverify runs after final GC exclusion is released but before
 // the top-level class-loading owner's destructor. Its same-heap/thread GC is
 // refundable observation only; it cannot grant stop or borrowed-root access.
 // Require the actual pause still owned, or the exact cleanup fully returned.
 if(o.Ready()&&o.cleanup_&&((o.phase_==Phase::kComplete&&o.cleanup_->Paused())||
     (o.phase_==Phase::kCleanupResumed&&o.cleanup_->CleanupCompleted()))){
  if(heap!=o.service_.heap_||self!=o.self_||!o.service_.current_||o.service_.current_()!=o.self_||starting==o.postfinal_gc_){o.Refuse("ordinary-source-postfinal-GC-owner-pair-mismatch");return;}
  o.postfinal_gc_=starting;return;
 }
 if(!o.Ready()||o.phase_!=Phase::kArmed){o.Refuse("ordinary-source-final-path-nested-GC-operation");return;}
 if(heap!=o.service_.heap_||self!=o.self_||!o.service_.current_||o.service_.current_()!=o.self_||starting==o.prefinal_gc_){
  o.Refuse("ordinary-source-prefinal-GC-owner-pair-mismatch");return;
 }
 o.prefinal_gc_=starting;
}
bool Observation::Finish(void* driver,bool success,Calibration* out) noexcept {
 Event(8);
 const bool ok=Ready()&&out&&driver==driver_&&success&&holder_&&!postfinal_gc_&&((!cleanup_&&phase_==Phase::kComplete)||(cleanup_&&phase_==Phase::kCleanupResumed&&cleanup_->CleanupCompleted()))&&State(service_.state_.RunnableState());
 if(ok){Calibration result;result.binding_=service_;result.run_body_=run_body_;std::memcpy(result.cause_,cause_,sizeof(cause_));result.long_=long_;result.valid_=true;
  result.publication_valid_=publication_profile_&&publications_>0;result.publication_linker_=publication_linker_;
  result.publication_pairs_=publication_pairs_;result.publication_collector_=publication_collector_;*out=result;}
 else if(!failed_)Refuse("ordinary-source-Run-did-not-complete-matching-final-owner");
 finished_=true;if(attached_){if(observation!=this)std::abort();observation=nullptr;attached_=false;}return ok;
}
Session::~Session(){Rollback();}
}
