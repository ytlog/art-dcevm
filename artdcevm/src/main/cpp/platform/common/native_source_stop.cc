#include "platform/common/native_source_stop.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
namespace artdcevm::native_source_stop {
namespace {
namespace a=abi::arm64::android_common;namespace s=a::source_stop;
thread_local Session* active=nullptr;
thread_local Session* finishing=nullptr;
thread_local Observation* observation=nullptr;
std::mutex installation;bool installed=false;
a::SuspendConstruct original_stop=nullptr;a::SuspendDestroy original_resume=nullptr;
s::MutatorOperation original_disable=nullptr,original_enable=nullptr;
s::StartGC original_start=nullptr;s::FinishGC original_finish=nullptr;
}
struct NativeHooks {
static void Construct(void* carrier,const char* cause,bool long_suspend) {
 if(Session::TransferConstruct(carrier,cause,long_suspend))return;
 if(!original_stop)std::abort();original_stop(carrier,cause,long_suspend);
 Observation::Constructed(carrier,cause,long_suspend);
}
static void Destroy(void* carrier) {
 if(Session::TransferDestroy(carrier))return;
 if(!original_resume)std::abort();original_resume(carrier);
 Observation::Destroyed(carrier);
}
static void Disable(void* heap,void* self){
 Session::SourceDisableBegin(heap,self);original_disable(heap,self);
 Observation::DisableBegin(heap,self);
}
static void Enable(void* heap,void* self){
 Session::SourceDisableEnd(heap,self,false);original_enable(heap,self);Observation::DisableEnd(heap,self);
 Session::SourceDisableEnd(heap,self,true);
}
static void Start(void* heap,void* self,int32_t cause,int32_t collector){
 Session::UnexpectedGC();Observation::GCOperation();original_start(heap,self,cause,collector);
}
static void Finish(void* heap,void* self,int32_t type){
 Session::UnexpectedGC();Observation::GCOperation();original_finish(heap,self,type);
}
};
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
}
bool Install(HookEngine& hooks,std::string* error) {
 std::lock_guard<std::mutex> guard(installation);if(installed)return true;
 auto ctor=hooks.FindSymbol("libart.so",a::kSuspendConstruct);
 auto dtor=hooks.FindSymbol("libart.so",a::kSuspendDestroy);
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
 a::SuspendAll scope{};reinterpret_cast<a::SuspendConstruct>(ctor)(&scope,"artdcevm source stop installation",false);
 void* tokens[6]{};size_t count=0;
 for(auto& binding:bindings){
  auto token=hooks.Install(binding.target,binding.proxy,binding.original,error);
  if(!token)break;if(!*binding.original)std::abort();tokens[count++]=token;
 }
 installed=count==6;
 if(!installed){std::string rollback;while(count)if(!hooks.Remove(tokens[--count],&rollback))std::abort();
  for(auto& binding:bindings)*binding.original=nullptr;}
 reinterpret_cast<a::SuspendDestroy>(dtor)(&scope);
 return installed;
}

bool Service::Bind(HookEngine& hooks,void* heap,const thread_suspend_service::Service& state,Service* out,std::string* error) {
 if(!out||!heap||!installed)return Fail(error,"Source observer lacks prepared receiver/installed transfer");
 Service c;c.heap_=heap;c.state_=state;
#define ART(member,name) c.member=reinterpret_cast<decltype(c.member)>(hooks.FindSymbol("libart.so",name))
 ART(mutator_,s::kMutator);ART(current_,s::kCurrent);ART(start_,s::kStartGC);ART(finish_,s::kFinishGC);
 ART(lock_,s::kLock);ART(unlock_,s::kUnlock);ART(stop_,a::kSuspendConstruct);ART(resume_,a::kSuspendDestroy);
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
bool Session::Begin(void* self,size_t extent) noexcept {
 if(phase_!=Phase::kEmpty||active||finishing||observation||!service_.calibrated_||!service_.state_.CanTransitionRunnable()||!service_.heap_||!service_.mutator_||*service_.mutator_!=service_.mutator_identity_||!self||service_.current_()!=self){failure_="source-stop-owner-or-lifecycle-mismatch";return false;}
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
 return self==self_&&active==this&&gc_&&suspended_&&exclusive_&&
   (phase_==Phase::kStopped||phase_==Phase::kFinal)&&Native();
}
bool Session::IsCollectedStop(void* self)const noexcept {return phase_==Phase::kStopped&&IsStopped(self);}
bool Session::IsFinalWindow(void* self)const noexcept {return phase_==Phase::kFinal&&IsStopped(self);}
bool Session::ContinueSource() noexcept {
 if(!IsStopped(self_)||phase_!=Phase::kStopped){failure_="source-stop-continuation-owner-mismatch";return false;}
 service_.unlock_(*service_.mutator_,self_);exclusive_=false;
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
void Session::Rollback() noexcept {
 if(phase_==Phase::kEmpty||phase_==Phase::kCompleted||phase_==Phase::kRolledBack)return;
 if(!SameOwner()||phase_==Phase::kFinal||phase_==Phase::kFinalResumed)std::abort();
 if(phase_==Phase::kContinuing){bool moved=false;if(!service_.state_.EnterNative(self_,extent_,&moved)||!moved||!Native())std::abort();service_.lock_(*service_.mutator_,self_);exclusive_=true;}
 if(!Native()||!exclusive_||!Reserved())std::abort();
 active=nullptr;service_.resume_(&storage_);suspended_=exclusive_=false;
 service_.finish_(service_.heap_,self_,s::kNoCollection);gc_=false;
 if(!service_.state_.RestoreRunnable(self_))std::abort();phase_=Phase::kRolledBack;
}
void Session::SourceDisableBegin(void* heap,void* self) noexcept {
 if(!active)return;auto& owner=*active;uint32_t state=UINT32_MAX;
 if(owner.phase_!=Phase::kContinuing||owner.source_disable_||heap!=owner.service_.heap_||self!=owner.self_||
    !owner.SameOwner()||!owner.service_.state_.State(self,owner.extent_,&state)||state!=owner.service_.state_.RunnableState())std::abort();
 owner.source_disable_=true;
}
void Session::SourceDisableEnd(void* heap,void* self,bool completed) noexcept {
 if(!finishing){if(active)std::abort();return;}auto& owner=*finishing;uint32_t state=UINT32_MAX;
 if(owner.phase_!=Phase::kFinalResumed||!owner.source_disable_||heap!=owner.service_.heap_||self!=owner.self_||
    !owner.SameOwner()||!owner.service_.state_.State(self,owner.extent_,&state)||state!=owner.service_.state_.RunnableState())std::abort();
 if(completed){owner.source_disable_=false;owner.phase_=Phase::kCompleted;finishing=nullptr;}
}
void Session::UnexpectedGC() noexcept {if(active)std::abort();}
bool Service::SameBinding(const Service& other)const noexcept {
 return state_.SameBinding(other.state_)&&heap_==other.heap_&&mutator_identity_==other.mutator_identity_&&mutator_&&*mutator_==mutator_identity_&&mutator_==other.mutator_&&current_==other.current_&&start_==other.start_&&finish_==other.finish_&&
   lock_==other.lock_&&unlock_==other.unlock_&&stop_==other.stop_&&resume_==other.resume_;
}
bool Service::Authorize(const Calibration& proof,const void* run) noexcept {
 if(!proof.valid_||!run||run!=proof.run_body_||!SameBinding(proof.binding_)||!state_.CanTransitionRunnable())return false;
 std::memcpy(final_cause_,proof.cause_,sizeof(final_cause_));final_long_=proof.long_;calibrated_=true;return true;
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
Observation::Observation(const Service& service,void* driver,const void* run,void* self,size_t extent) noexcept:
 service_(service),driver_(driver),run_body_(run),self_(self),extent_(extent){
 trace_.expected_runnable=service_.state_.RunnableState();Event(1);
 if(observation||active||finishing||!driver||!run||!service_.heap_||!State(service_.state_.RunnableState())){Refuse("ordinary-source-observation-owner-mismatch");return;}
 observation=this;attached_=true;failure_="";
}
Observation::~Observation(){if(attached_){if(observation!=this)std::abort();observation=nullptr;}}
bool Observation::Ready()const noexcept{return attached_&&!failed_&&!finished_&&observation==this;}
bool Observation::Collected(void* driver,const void* holder) noexcept {
 Event(2);trace_.collected_ready=Ready();trace_.collected_driver_match=driver==driver_;trace_.collected_holder_present=holder!=nullptr;
 if(!Ready()||phase_!=Phase::kRun||driver!=driver_||!holder||!State(service_.state_.RunnableState())){
  Refuse("ordinary-source-Collected-owner-order-mismatch");return false;}
 holder_=holder;phase_=Phase::kArmed;return true;
}
void Observation::DisableBegin(void* heap,void* self) noexcept {
 if(!observation)return;auto& o=*observation;o.Event(3);if(o.phase_==Phase::kRun)return;
 if(!o.Ready()||o.phase_!=Phase::kArmed||heap!=o.service_.heap_||self!=o.self_||!o.State(o.service_.state_.RunnableState())){
  o.Refuse("ordinary-source-original-GC-disable-owner-order-mismatch");return;}
 o.phase_=Phase::kDisabled;
}
void Observation::Constructed(void* carrier,const char* cause,bool long_suspend) noexcept {
 if(!observation)return;auto& o=*observation;o.Event(4);if(o.phase_==Phase::kRun)return;
 if(!o.Ready()||o.phase_!=Phase::kDisabled||!carrier||!cause||!o.State(abi::arm64::android11::kNative)){
  o.Refuse("ordinary-source-original-final-stop-owner-order-mismatch");return;}
 size_t length=0;while(length<sizeof(o.cause_)&&cause[length])++length;
 if(!length||length==sizeof(o.cause_)){o.Refuse("ordinary-source-original-final-stop-cause-unbounded");return;}
 std::memcpy(o.cause_,cause,length+1);o.long_=long_suspend;o.carrier_=carrier;o.phase_=Phase::kStopped;
}
void Observation::Destroyed(void* carrier) noexcept {
 if(!observation)return;auto& o=*observation;o.Event(5);if(o.phase_==Phase::kRun)return;
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
void Observation::GCOperation() noexcept {
 if(observation)observation->Event(7);
 if(observation&&observation->phase_!=Phase::kRun)observation->Refuse("ordinary-source-final-path-nested-GC-operation");
}
bool Observation::Finish(void* driver,bool success,Calibration* out) noexcept {
 Event(8);
 const bool ok=Ready()&&out&&driver==driver_&&success&&holder_&&phase_==Phase::kComplete&&State(service_.state_.RunnableState());
 if(ok){Calibration result;result.binding_=service_;result.run_body_=run_body_;std::memcpy(result.cause_,cause_,sizeof(cause_));result.long_=long_;result.valid_=true;*out=result;}
 else if(!failed_)Refuse("ordinary-source-Run-did-not-complete-matching-final-owner");
 finished_=true;if(attached_){if(observation!=this)std::abort();observation=nullptr;attached_=false;}return ok;
}
Session::~Session(){Rollback();}
}
