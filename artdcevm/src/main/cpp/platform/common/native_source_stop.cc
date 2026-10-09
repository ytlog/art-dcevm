#include "platform/common/native_source_stop.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
namespace artdcevm::native_source_stop {
namespace {
namespace a=abi::arm64::android_common;namespace s=a::source_stop;
thread_local Session* active=nullptr;
std::mutex installation;bool installed=false;
a::SuspendConstruct original_stop=nullptr;a::SuspendDestroy original_resume=nullptr;
void Construct(void* carrier,const char* cause,bool long_suspend) {
 if(Session::TransferConstruct(carrier,cause,long_suspend))return;
 if(!original_stop)std::abort();original_stop(carrier,cause,long_suspend);
}
void Destroy(void* carrier) {
 if(Session::TransferDestroy(carrier))return;
 if(!original_resume)std::abort();original_resume(carrier);
}
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
 a::SuspendAll scope{};reinterpret_cast<a::SuspendConstruct>(ctor)(&scope,"artdcevm source stop installation",false);
 void* token=hooks.Install(ctor,reinterpret_cast<void*>(Construct),reinterpret_cast<void**>(&original_stop),error);
 if(token&&!original_stop)std::abort();
 void* second=token?hooks.Install(dtor,reinterpret_cast<void*>(Destroy),reinterpret_cast<void**>(&original_resume),error):nullptr;
 if(second&&!original_resume)std::abort();
 if(token&&!second){std::string rollback;if(!hooks.Remove(token,&rollback))std::abort();original_stop=nullptr;}
 // During hook installation this source reservation is inactive; the original
 // destructor always owns the actual suspension acquired above.
 reinterpret_cast<a::SuspendDestroy>(dtor)(&scope);
 installed=token&&second;return installed;
}
bool Service::Bind(HookEngine& hooks,void* heap,const thread_suspend_service::Service& state,Service* out,std::string* error) {
 if(!out||!heap||!installed||!state.CanTransitionRunnable())return Fail(error,"Source stop lacks prepared receiver/paired pure transitions/installed transfer");
 Service c;c.heap_=heap;c.state_=state;
#define ART(member,name) c.member=reinterpret_cast<decltype(c.member)>(hooks.FindSymbol("libart.so",name))
 ART(mutator_,s::kMutator);ART(current_,s::kCurrent);ART(start_,s::kStartGC);ART(finish_,s::kFinishGC);
 ART(lock_,s::kLock);ART(unlock_,s::kUnlock);ART(stop_,a::kSuspendConstruct);ART(resume_,a::kSuspendDestroy);
#undef ART
 if(!c.mutator_||!*c.mutator_||!c.current_||!c.start_||!c.finish_||!c.lock_||!c.unlock_||!c.stop_||!c.resume_)
   return Fail(error,"Source stop lacks exact native heap/mutator/scope leaves");
 *out=c;return true;
}
bool Session::SameOwner()const noexcept{return self_&&service_.current_&&service_.current_()==self_;}
bool Session::Native()const noexcept {
 uint32_t state=UINT32_MAX;return SameOwner()&&service_.state_.State(self_,extent_,&state)&&state==abi::arm64::android11::kNative;
}
bool Session::Begin(void* self,size_t extent) noexcept {
 if(phase_!=Phase::kEmpty||active||!service_.heap_||!self||service_.current_()!=self){failure_="source-stop-owner-or-lifecycle-mismatch";return false;}
 self_=self;extent_=extent;uint32_t state=UINT32_MAX;
 if(!service_.state_.State(self_,extent_,&state)||state!=abi::arm64::android13::kRunnable){failure_="source-stop-requires-calibrated-Runnable-owner";return false;}
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
bool Session::ContinueSource() noexcept {
 if(!IsStopped(self_)||phase_!=Phase::kStopped){failure_="source-stop-continuation-owner-mismatch";return false;}
 service_.unlock_(*service_.mutator_,self_);exclusive_=false;
 if(!service_.state_.RestoreRunnable(self_))std::abort();
 uint32_t state=UINT32_MAX;if(!SameOwner()||!service_.state_.State(self_,extent_,&state)||state!=abi::arm64::android13::kRunnable)std::abort();
 phase_=Phase::kContinuing;return true;
}
bool Session::TransferConstruct(void* carrier,const char* cause,bool long_suspend) noexcept {
 if(!active)return false;auto& owner=*active;
 // A constructor in the stopped scan is unrelated to the transfer and would
 // nest global suspension. It is invalid, not an invitation to run the leaf.
 if(owner.phase_!=Phase::kContinuing||!carrier||!cause||std::strcmp(cause,s::kFinalCause)||!long_suspend||!owner.Native()||!owner.Reserved()||owner.exclusive_)std::abort();
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
 owner.phase_=Phase::kCompleted;return true;
}
void Session::Rollback() noexcept {
 if(phase_==Phase::kEmpty||phase_==Phase::kCompleted||phase_==Phase::kRolledBack)return;
 if(!SameOwner()||phase_==Phase::kFinal)std::abort();
 if(phase_==Phase::kContinuing){bool moved=false;if(!service_.state_.EnterNative(self_,extent_,&moved)||!moved||!Native())std::abort();service_.lock_(*service_.mutator_,self_);exclusive_=true;}
 if(!Native()||!exclusive_||!Reserved())std::abort();
 active=nullptr;service_.resume_(&storage_);suspended_=exclusive_=false;
 service_.finish_(service_.heap_,self_,s::kNoCollection);gc_=false;
 if(!service_.state_.RestoreRunnable(self_))std::abort();phase_=Phase::kRolledBack;
}
Session::~Session(){Rollback();}
}
