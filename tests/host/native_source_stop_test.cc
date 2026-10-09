#include "platform/common/native_source_calibration_bridge.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace artdcevm::native_owned_removal {
thread_local PassiveObserver fixture_observer;
bool SetPassiveObserver(const PassiveObserver* observer) noexcept {
 if(!observer){fixture_observer={};return true;}
 if(fixture_observer.begin)return false;
 fixture_observer=*observer;return true;
}
}

namespace {
namespace a=abi::arm64::android_common;namespace s=a::source_stop;
struct Thread {uint32_t flags=92u<<24;} self,foreign;
int heap,mutex;void* mutator=&mutex;
bool exclusive=false,gc=false,bad_transition=false,foreign_current=false;
int stops=0,resumes=0,locks=0,unlocks=0,gc_starts=0,gc_finishes=0;
a::SuspendConstruct stop_proxy=nullptr;a::SuspendDestroy resume_proxy=nullptr;
s::MutatorOperation disable_proxy=nullptr,enable_proxy=nullptr;
s::StartGC start_proxy=nullptr;s::FinishGC finish_proxy=nullptr;
int disable_count=0;unsigned shift=24;uint32_t runnable_state=0;
void DisableMoving(void* p,void* thread){assert(p==&heap&&thread==&self&&self.flags==(runnable_state<<shift)&&!exclusive&&disable_count==0);++disable_count;}
void EnableMoving(void* p,void* thread){assert(p==&heap&&thread==&self&&self.flags==(runnable_state<<shift)&&!exclusive&&disable_count==1);--disable_count;}
void* Current(){return foreign_current?&foreign:&self;}
void Native(void* p){assert(p==&self&&self.flags==(runnable_state<<shift)&&!exclusive);if(!bad_transition)self.flags=92u<<shift;}
void Runnable(void* p){assert(p==&self&&self.flags==92u<<shift&&!exclusive);self.flags=runnable_state<<shift;}
void Start(void* p,void* thread,int32_t cause,int32_t collector){assert(p==&heap&&thread==&self&&cause==8&&collector==1&&self.flags==92u<<shift&&!gc&&!exclusive);gc=true;++gc_starts;}
void Finish(void* p,void* thread,int32_t type){assert(p==&heap&&thread==&self&&type==0&&gc&&!exclusive);gc=false;++gc_finishes;}
void Stop(void*,const char*,bool){assert(self.flags==92u<<shift&&!exclusive&&stops==resumes);exclusive=true;++stops;}
void Resume(void*){assert(self.flags==92u<<shift&&exclusive&&stops==resumes+1);exclusive=false;++resumes;}
void Lock(void* p,void* thread){assert(p==&mutex&&thread==&self&&self.flags==92u<<shift&&gc&&!exclusive&&stops==resumes+1);exclusive=true;++locks;}
void Unlock(void* p,void* thread){assert(p==&mutex&&thread==&self&&self.flags==92u<<shift&&gc&&exclusive);exclusive=false;++unlocks;}
class Hooks:public HookEngine {
 public:const char* missing=nullptr;bool bad_ctor_alias=false,bad_dtor_alias=false,duplicate_target=false;int installations=0;bool fail_second=false;int fail_at=0;int removed=0;
 bool Initialize(std::string*)override{return true;}
 template<class T>static void* Fn(T f){return reinterpret_cast<void*>(f);}
 void* FindSymbol(const char*,const char* name)override {
  if(missing&&!std::strcmp(missing,name))return nullptr;
  if(bad_ctor_alias&&!std::strcmp(name,"_ZN3art16ScopedSuspendAllC1EPKcb"))return Fn(Current);
  if(bad_dtor_alias&&!std::strcmp(name,"_ZN3art16ScopedSuspendAllD1Ev"))return Fn(Current);
  if(duplicate_target&&!std::strcmp(name,s::kFinishGC))return Fn(Start);
  if(!std::strcmp(name,a::kSuspendConstruct)||!std::strcmp(name,"_ZN3art16ScopedSuspendAllC1EPKcb"))return Fn(stop_proxy?stop_proxy:Stop);
  if(!std::strcmp(name,a::kSuspendDestroy)||!std::strcmp(name,"_ZN3art16ScopedSuspendAllD1Ev"))return Fn(resume_proxy?resume_proxy:Resume);
  if(!std::strcmp(name,abi::arm64::android13::kToNative))return Fn(Native);
  if(!std::strcmp(name,abi::arm64::android13::kToRunnable))return Fn(Runnable);
  if(!std::strcmp(name,s::kCurrent))return Fn(Current);
  if(!std::strcmp(name,s::kMutator))return &mutator;
  if(!std::strcmp(name,s::kStartGC))return Fn(start_proxy?start_proxy:Start);
  if(!std::strcmp(name,s::kFinishGC))return Fn(finish_proxy?finish_proxy:Finish);
  if(!std::strcmp(name,s::kDisableMoving))return Fn(disable_proxy?disable_proxy:DisableMoving);
  if(!std::strcmp(name,s::kEnableMoving))return Fn(enable_proxy?enable_proxy:EnableMoving);
  if(!std::strcmp(name,s::kLock))return Fn(Lock);
  if(!std::strcmp(name,s::kUnlock))return Fn(Unlock);
  return nullptr;
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override {
  assert(exclusive&&self.flags==92u<<shift);++installations;
  if((fail_second&&installations==2)||fail_at==installations)return nullptr;
  *original=target;
  if(target==Fn(Stop))stop_proxy=reinterpret_cast<a::SuspendConstruct>(proxy);
  else if(target==Fn(Resume))resume_proxy=reinterpret_cast<a::SuspendDestroy>(proxy);
  else if(target==Fn(DisableMoving))disable_proxy=reinterpret_cast<s::MutatorOperation>(proxy);
  else if(target==Fn(EnableMoving))enable_proxy=reinterpret_cast<s::MutatorOperation>(proxy);
  else if(target==Fn(Start))start_proxy=reinterpret_cast<s::StartGC>(proxy);
  else{assert(target==Fn(Finish));finish_proxy=reinterpret_cast<s::FinishGC>(proxy);}
  return reinterpret_cast<void*>(uintptr_t(installations));
 }
 bool Remove(void* token,std::string*)override{assert(exclusive);++removed;
  switch(reinterpret_cast<uintptr_t>(token)){
   case 1:stop_proxy=nullptr;break;case 2:resume_proxy=nullptr;break;
   case 3:disable_proxy=nullptr;break;case 4:enable_proxy=nullptr;break;
   case 5:start_proxy=nullptr;break;case 6:finish_proxy=nullptr;break;default:assert(false);
  }return true;
 }
};
thread_suspend_service::Service State(Hooks& h,thread_suspend_service::Declaration declaration=thread_suspend_service::Declaration::kModern24){
 thread_suspend_service::Service state;assert(thread_suspend_service::Service::Bind(declaration,[&](const char* n){return h.FindSymbol("libart.so",n);},&state));return state;
}
native_source_stop::Calibration Calibrate(native_source_stop::Service& service){
 int driver,run,holder;native_source_stop::Calibration proof;self.flags=runnable_state<<shift;
 native_source_stop::Observation observation(service,&driver,&run,&self,sizeof(self));assert(observation.Ready());
 assert(observation.Collected(&driver,&holder));disable_proxy(&heap,&self);Native(&self);
 unsigned char carrier;stop_proxy(&carrier,s::kFinalCause,true);resume_proxy(&carrier);Runnable(&self);enable_proxy(&heap,&self);
 assert(observation.Finish(&driver,true,&proof)&&proof.Valid()&&service.Authorize(proof,&run));return proof;
}

void BridgeProofs(const native_source_stop::Service& service){
 int driver,run,holder;
 for(int mode=0;mode<7;++mode){
  self.flags=runnable_state<<shift;
  {
   native_source_calibration::Bridge bridge(service,&self,sizeof(self));
   auto observer=native_owned_removal::fixture_observer;assert(observer.begin);
   {native_source_calibration::Bridge nested(service,&self,sizeof(self));assert(!nested.Authorized());}
   assert(native_owned_removal::fixture_observer.state==observer.state);
   observer.begin(&driver,&run,mode==3?&foreign:&self,observer.state);
   if(mode!=4)observer.collected(&driver,&holder,observer.state);
   disable_proxy(&heap,&self);Native(&self);unsigned char carrier;
   stop_proxy(&carrier,"actual ordinary source bridge",false);resume_proxy(&carrier);Runnable(&self);
   if(mode!=5)enable_proxy(&heap,&self);
   observer.end(&driver,mode==1?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE,mode!=2,observer.state);
   assert(bridge.Authorized()==(mode==0||mode==6));
   if(bridge.Authorized())assert(bridge.Binding().Calibrated());
   if(mode==5)enable_proxy(&heap,&self);
  }
  assert(!native_owned_removal::fixture_observer.begin);
 }
 // Exception/early-exit destroys an attached native observation and TLS subscriber.
 try{native_source_calibration::Bridge bridge(service,&self,sizeof(self));
  auto observer=native_owned_removal::fixture_observer;
  observer.begin(&driver,&run,&self,observer.state);throw 1;
 }catch(int){}
 assert(!native_owned_removal::fixture_observer.begin);
}

void ObservationNegatives(native_source_stop::Service& service,Hooks& hooks){
 int driver,run,holder,wrong_driver,wrong_run,other_heap;
 native_source_stop::Calibration empty;
 assert(!service.Authorize(empty,&run));
 self.flags=runnable_state<<shift;
 for(int mode=0;mode!=7;++mode){
  native_source_stop::Service bound=service;
  if(mode==3)assert(native_source_stop::Service::Bind(hooks,&other_heap,State(hooks),&bound,nullptr));
  native_source_stop::Observation observation(bound,&driver,&run,&self,sizeof(self));assert(observation.Ready());
  if(mode==0)assert(!observation.Collected(&wrong_driver,&holder));
  else if(mode!=1)assert(observation.Collected(&driver,&holder));
  if(mode==4){Native(&self);start_proxy(&heap,&self,8,1);finish_proxy(&heap,&self,0);Runnable(&self);}
  disable_proxy(&heap,&self);Native(&self);unsigned char carrier,wrong_carrier;
  stop_proxy(&carrier,s::kFinalCause,true);resume_proxy(mode==5?&wrong_carrier:&carrier);Runnable(&self);
  native_source_stop::Calibration proof;
  if(mode==6){assert(!observation.Finish(&driver,true,&proof));enable_proxy(&heap,&self);}
  else{enable_proxy(&heap,&self);assert(!observation.Finish(&driver,mode!=2,&proof));}
  assert(!proof.Valid()&&!gc&&!exclusive&&disable_count==0);
 }
 // Observed original cause/long flag is authority; no fixed-source string grant.
 native_source_stop::Service alternate=service;native_source_stop::Calibration proof;
 {native_source_stop::Observation observation(alternate,&driver,&run,&self,sizeof(self));assert(observation.Collected(&driver,&holder));
  disable_proxy(&heap,&self);Native(&self);unsigned char carrier;stop_proxy(&carrier,"observed ordinary source final",false);resume_proxy(&carrier);Runnable(&self);enable_proxy(&heap,&self);
  assert(observation.Finish(&driver,true,&proof));}
 assert(proof.Valid()&&!alternate.Authorize(proof,&wrong_run)&&alternate.Authorize(proof,&run));
 {native_source_stop::Session owner(alternate);assert(owner.Begin(&self,sizeof(self))&&owner.ContinueSource());
  disable_proxy(&heap,&self);Native(&self);unsigned char carrier;stop_proxy(&carrier,"observed ordinary source final",false);resume_proxy(&carrier);
  assert(!owner.Completed());Runnable(&self);enable_proxy(&heap,&self);assert(owner.Completed());}
 assert(!gc&&!exclusive&&disable_count==0);
}

void LegacyObservation(Hooks& hooks){
 shift=16;runnable_state=67;self.flags=runnable_state<<shift;
 auto state=State(hooks,thread_suspend_service::Declaration::kLegacy16);
 assert(state.RunnableState()==67&&!state.CanTransitionRunnable());
 native_source_stop::Service service;assert(native_source_stop::Service::Bind(hooks,&heap,state,&service,nullptr));
 int driver,run,holder;native_source_stop::Calibration proof;
 native_source_stop::Observation observation(service,&driver,&run,&self,sizeof(self));assert(observation.Ready());
 assert(observation.Collected(&driver,&holder));disable_proxy(&heap,&self);Native(&self);
 unsigned char carrier;stop_proxy(&carrier,s::kFinalCause,true);resume_proxy(&carrier);Runnable(&self);enable_proxy(&heap,&self);
 assert(observation.Finish(&driver,true,&proof)&&proof.Valid());
 assert(!service.Authorize(proof,&run)&&!service.Calibrated());
 const int starts=gc_starts,stop_count=stops;
 {native_source_stop::Session owner(service);assert(!owner.Begin(&self,sizeof(self))&&self.flags==(67u<<16));}
 assert(gc_starts==starts&&stops==stop_count&&!gc&&!exclusive&&disable_count==0);
 shift=24;runnable_state=0;self.flags=0;
}

}
int main(int argc,char** argv){
 Hooks hooks;std::string error;
 if(argc==2&&(!std::strcmp(argv[1],"bad-ctor-alias")||!std::strcmp(argv[1],"bad-dtor-alias")||!std::strcmp(argv[1],"duplicate-target")||!std::strcmp(argv[1],"missing-heap-gc"))){
  hooks.bad_ctor_alias=!std::strcmp(argv[1],"bad-ctor-alias");hooks.bad_dtor_alias=!std::strcmp(argv[1],"bad-dtor-alias");
  hooks.duplicate_target=!std::strcmp(argv[1],"duplicate-target");if(!std::strcmp(argv[1],"missing-heap-gc"))hooks.missing=s::kDisableMoving;
  assert(!native_source_stop::Install(hooks,&error)&&hooks.installations==0&&stops==0&&resumes==0&&!exclusive&&!gc);
  std::cout<<"native identity rejection before installation PASS\n";return 0;
 }
 if(argc==2&&!std::strncmp(argv[1],"install-rollback",16)){
  hooks.fail_at=argv[1][16]?argv[1][16]-'0':2;assert(hooks.fail_at>=1&&hooks.fail_at<=6);
  assert(!native_source_stop::Install(hooks,&error));assert(hooks.removed==hooks.fail_at-1&&!exclusive&&stops==resumes&&!stop_proxy&&!resume_proxy&&!disable_proxy&&!enable_proxy&&!start_proxy&&!finish_proxy);std::cout<<"installation rollback PASS\n";return 0;
 }
 assert(native_source_stop::Install(hooks,&error));assert(!exclusive&&stops==resumes&&hooks.installations==6);
 auto state=State(hooks);native_source_stop::Service service;
 assert(!native_source_stop::Service::Bind(hooks,nullptr,state,&service,&error));
 native_source_stop::Service legacy;assert(native_source_stop::Service::Bind(hooks,&heap,State(hooks,thread_suspend_service::Declaration::kLegacy16),&legacy,&error)&&!legacy.Calibrated());
 assert(!state.SameBinding(State(hooks,thread_suspend_service::Declaration::kLegacy16))&&state.SameBinding(State(hooks)));
 hooks.missing=s::kStartGC;assert(!native_source_stop::Service::Bind(hooks,&heap,state,&service,&error));hooks.missing=nullptr;
 assert(native_source_stop::Service::Bind(hooks,&heap,state,&service,&error));
 BridgeProofs(service);
 LegacyObservation(hooks);
 ObservationNegatives(service,hooks);
 assert(!service.Calibrated());
 self.flags=runnable_state<<shift;{native_source_stop::Session uncalibrated(service);assert(!uncalibrated.Begin(&self,sizeof(self))&&!gc&&!exclusive);}
 Calibrate(service);
 {int other_mutator;mutator=&other_mutator;native_source_stop::Session changed(service);assert(!changed.Begin(&self,sizeof(self))&&self.flags==(runnable_state<<shift)&&!gc&&!exclusive);mutator=&mutex;}
 self.flags=92u<<shift;
 // No callback/phase boolean alone can grant any stop permission.
 native_source_stop::Session unopened(service);assert(!unopened.IsStopped(&self)&&!unopened.Reserved());
 assert(!unopened.Begin(&self,sizeof(self))); // ordinary Native JNI is not source Runnable
 assert(!unopened.Begin(&foreign,sizeof(foreign)));
 self.flags=runnable_state<<shift;
 {native_source_stop::Session owner(service);assert(!owner.Begin(&self,0)&&!gc&&!exclusive);}
 bad_transition=true;
 {native_source_stop::Session owner(service);assert(!owner.Begin(&self,sizeof(self))&&!gc&&!exclusive&&self.flags==(runnable_state<<shift));}
 bad_transition=false;
 // A real refundable scan refusal unwinds the actual reservation, not just flags.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self))&&owner.IsStopped(&self)&&gc&&exclusive);
  assert(!owner.IsStopped(&foreign));foreign_current=true;assert(!owner.IsStopped(&self));foreign_current=false;
  native_source_stop::Session nested(service);const int count=gc_starts;assert(!nested.Begin(&self,sizeof(self))&&gc_starts==count);
 }
 assert(!gc&&!exclusive&&self.flags==(runnable_state<<shift)&&stops==resumes);
 // Original source continuation preserves actual global suspension and GC.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self)));
  const int captured=stops;assert(owner.ContinueSource()&&gc&&!exclusive&&owner.Reserved()&&!owner.IsStopped(&self)&&self.flags==(runnable_state<<shift)&&stops==captured&&stops==resumes+1);
  if(argc==2&&!std::strcmp(argv[1],"wrong-final")){disable_proxy(&heap,&self);Native(&self);unsigned char wrong;stop_proxy(&wrong,"unrelated stop",true);assert(false);}
  disable_proxy(&heap,&self);Native(&self);unsigned char original_source_carrier;
  stop_proxy(&original_source_carrier,s::kFinalCause,true);
  assert(owner.IsStopped(&self)&&stops==captured&&gc&&exclusive);
  resume_proxy(&original_source_carrier);
  assert(!owner.Completed()&&!gc&&!exclusive&&stops==resumes&&self.flags==92u<<shift);
  Runnable(&self);enable_proxy(&heap,&self);assert(owner.Completed()&&disable_count==0);
 }
 // Abnormal source return still releases the owned reservation from Runnable.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self))&&owner.ContinueSource());}
 assert(!gc&&!exclusive&&self.flags==(runnable_state<<shift)&&stops==resumes&&gc_starts==gc_finishes&&locks==unlocks);
 std::cout<<"continuous original stop/GC ownership, transfer and rollback PASS\n";return 0;
}
