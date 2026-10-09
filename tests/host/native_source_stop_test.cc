#include "platform/common/native_source_stop.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace {
namespace a=abi::arm64::android_common;namespace s=a::source_stop;
struct Thread {uint32_t flags=92u<<24;} self,foreign;
int heap,mutex;void* mutator=&mutex;
bool exclusive=false,gc=false,bad_transition=false,foreign_current=false;
int stops=0,resumes=0,locks=0,unlocks=0,gc_starts=0,gc_finishes=0;
a::SuspendConstruct stop_proxy=nullptr;a::SuspendDestroy resume_proxy=nullptr;
s::MutatorOperation disable_proxy=nullptr,enable_proxy=nullptr;
s::StartGC start_proxy=nullptr;s::FinishGC finish_proxy=nullptr;
int disable_count=0;
void DisableMoving(void* p,void* thread){assert(p==&heap&&thread==&self&&self.flags==0&&!exclusive&&disable_count==0);++disable_count;}
void EnableMoving(void* p,void* thread){assert(p==&heap&&thread==&self&&self.flags==0&&!exclusive&&disable_count==1);--disable_count;}
void* Current(){return foreign_current?&foreign:&self;}
void Native(void* p){assert(p==&self&&self.flags==0&&!exclusive);if(!bad_transition)self.flags=92u<<24;}
void Runnable(void* p){assert(p==&self&&self.flags==92u<<24&&!exclusive);self.flags=0;}
void Start(void* p,void* thread,int32_t cause,int32_t collector){assert(p==&heap&&thread==&self&&cause==8&&collector==1&&self.flags==92u<<24&&!gc&&!exclusive);gc=true;++gc_starts;}
void Finish(void* p,void* thread,int32_t type){assert(p==&heap&&thread==&self&&type==0&&gc&&!exclusive);gc=false;++gc_finishes;}
void Stop(void*,const char*,bool){assert(self.flags==92u<<24&&!exclusive&&stops==resumes);exclusive=true;++stops;}
void Resume(void*){assert(self.flags==92u<<24&&exclusive&&stops==resumes+1);exclusive=false;++resumes;}
void Lock(void* p,void* thread){assert(p==&mutex&&thread==&self&&self.flags==92u<<24&&gc&&!exclusive&&stops==resumes+1);exclusive=true;++locks;}
void Unlock(void* p,void* thread){assert(p==&mutex&&thread==&self&&self.flags==92u<<24&&gc&&exclusive);exclusive=false;++unlocks;}
class Hooks:public HookEngine {
 public:const char* missing=nullptr;int installations=0;bool fail_second=false;int fail_at=0;int removed=0;
 bool Initialize(std::string*)override{return true;}
 template<class T>static void* Fn(T f){return reinterpret_cast<void*>(f);}
 void* FindSymbol(const char*,const char* name)override {
  if(missing&&!std::strcmp(missing,name))return nullptr;
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
  assert(exclusive&&self.flags==92u<<24);++installations;
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
 int driver,run,holder;native_source_stop::Calibration proof;self.flags=0;
 native_source_stop::Observation observation(service,&driver,&run,&self,sizeof(self));assert(observation.Ready());
 assert(observation.Collected(&driver,&holder));disable_proxy(&heap,&self);Native(&self);
 unsigned char carrier;stop_proxy(&carrier,s::kFinalCause,true);resume_proxy(&carrier);Runnable(&self);enable_proxy(&heap,&self);
 assert(observation.Finish(&driver,true,&proof)&&proof.Valid()&&service.Authorize(proof,&run));return proof;
}

void ObservationNegatives(native_source_stop::Service& service,Hooks& hooks){
 int driver,run,holder,wrong_driver,wrong_run,other_heap;
 native_source_stop::Calibration empty;
 assert(!service.Authorize(empty,&run));
 self.flags=0;
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

}
int main(int argc,char** argv){
 Hooks hooks;std::string error;
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
 ObservationNegatives(service,hooks);
 assert(!service.Calibrated());
 self.flags=0;{native_source_stop::Session uncalibrated(service);assert(!uncalibrated.Begin(&self,sizeof(self))&&!gc&&!exclusive);}
 Calibrate(service);
 {int other_mutator;mutator=&other_mutator;native_source_stop::Session changed(service);assert(!changed.Begin(&self,sizeof(self))&&self.flags==0&&!gc&&!exclusive);mutator=&mutex;}
 self.flags=92u<<24;
 // No callback/phase boolean alone can grant any stop permission.
 native_source_stop::Session unopened(service);assert(!unopened.IsStopped(&self)&&!unopened.Reserved());
 assert(!unopened.Begin(&self,sizeof(self))); // ordinary Native JNI is not source Runnable
 assert(!unopened.Begin(&foreign,sizeof(foreign)));
 self.flags=0;
 {native_source_stop::Session owner(service);assert(!owner.Begin(&self,0)&&!gc&&!exclusive);}
 bad_transition=true;
 {native_source_stop::Session owner(service);assert(!owner.Begin(&self,sizeof(self))&&!gc&&!exclusive&&self.flags==0);}
 bad_transition=false;
 // A real refundable scan refusal unwinds the actual reservation, not just flags.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self))&&owner.IsStopped(&self)&&gc&&exclusive);
  assert(!owner.IsStopped(&foreign));foreign_current=true;assert(!owner.IsStopped(&self));foreign_current=false;
  native_source_stop::Session nested(service);const int count=gc_starts;assert(!nested.Begin(&self,sizeof(self))&&gc_starts==count);
 }
 assert(!gc&&!exclusive&&self.flags==0&&stops==resumes);
 // Original source continuation preserves actual global suspension and GC.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self)));
  const int captured=stops;assert(owner.ContinueSource()&&gc&&!exclusive&&owner.Reserved()&&!owner.IsStopped(&self)&&self.flags==0&&stops==captured&&stops==resumes+1);
  if(argc==2&&!std::strcmp(argv[1],"wrong-final")){disable_proxy(&heap,&self);Native(&self);unsigned char wrong;stop_proxy(&wrong,"unrelated stop",true);assert(false);}
  disable_proxy(&heap,&self);Native(&self);unsigned char original_source_carrier;
  stop_proxy(&original_source_carrier,s::kFinalCause,true);
  assert(owner.IsStopped(&self)&&stops==captured&&gc&&exclusive);
  resume_proxy(&original_source_carrier);
  assert(!owner.Completed()&&!gc&&!exclusive&&stops==resumes&&self.flags==92u<<24);
  Runnable(&self);enable_proxy(&heap,&self);assert(owner.Completed()&&disable_count==0);
 }
 // Abnormal source return still releases the owned reservation from Runnable.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self))&&owner.ContinueSource());}
 assert(!gc&&!exclusive&&self.flags==0&&stops==resumes&&gc_starts==gc_finishes&&locks==unlocks);
 std::cout<<"continuous original stop/GC ownership, transfer and rollback PASS\n";return 0;
}
