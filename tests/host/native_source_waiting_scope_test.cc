#include "platform/common/native_source_calibration_bridge.h"
#include "platform/common/native_cache_service.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <new>
static bool noalloc=false;
void* operator new(std::size_t n){if(noalloc)throw std::bad_alloc();if(void*p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
using namespace artdcevm;
namespace artdcevm::native_owned_removal {
thread_local PassiveObserver waiting_fixture_observer;
bool ReadPassiveTrace(PassiveTrace*out)noexcept{if(!out)return false;*out={};out->original_calls=1;out->whole_collections=1;return true;}
bool SetPassiveObserver(const PassiveObserver*p)noexcept{
 if(!p){waiting_fixture_observer={};return true;}
 if(waiting_fixture_observer.begin)return false;waiting_fixture_observer=*p;return true;
}
}
namespace {
namespace a=abi::arm64::android_common;namespace s=a::source_stop;
uint32_t flags=92u<<16;int heap,mutator_mutex,gc_mutex,other_mutex,self_driver,holder,body;
void* mutator=&mutator_mutex;void* list_mutex=&other_mutex;bool gc=false,exclusive=false,gc_lock=false;
int starts=0,finishes=0,stops=0,resumes=0,callbacks=0,wait_result=0;
const char* mode="";bool vendor=false;
uint32_t gc_argument=0x8a51729bu,last_vendor_argument=0;int vendor_original_calls=0;
using Wait=int32_t(*)(void*,int32_t,void*);
Wait wait_proxy=nullptr;s::MutatorOperation ml_proxy=nullptr,mu_proxy=nullptr;
using VendorLock=void(*)(void*,void*,uint32_t);VendorLock vendor_proxy=nullptr;
abi::common::SuspendConstruct stop_proxy=nullptr;abi::common::SuspendDestroy resume_proxy=nullptr;
s::StartGC start_proxy=nullptr;s::FinishGC finish_proxy=nullptr;
s::MutatorOperation disable_proxy=nullptr,enable_proxy=nullptr;
void* Current(){return &flags;}
void Native(){assert(flags==67u<<16&&!exclusive);flags=92u<<16;}
void Runnable(){assert(flags==92u<<16&&!exclusive);flags=67u<<16;}
void ML(void*m,void*t){assert(t==&flags||t==&body);if(m==&gc_mutex){assert(!gc_lock);gc_lock=true;}}
void VendorML(void*m,void*t,uint32_t argument){last_vendor_argument=argument;++vendor_original_calls;ML(m,t);}
void ActualLock(void*m,void*t,uint32_t argument=0){
 if(vendor){assert(vendor_proxy);vendor_proxy(m,t,argument);assert(last_vendor_argument==argument);}
 else {assert(ml_proxy);ml_proxy(m,t);}
}
void MU(void*m,void*t){assert(t==&flags);if(m==&gc_mutex){assert(gc_lock);gc_lock=false;}}
int32_t W(void*p,int32_t c,void*t){assert((p==&heap||p==&other_mutex)&&(c==8||c==9)&&(t==&flags||t==&body)&&gc_lock&&flags==74u<<16);
 // Actual ConditionVariable wait releases and re-acquires the exact GC mutex.
 // This must never run the stopped callback before the owner has been assigned.
 mu_proxy(&gc_mutex,&flags);assert(callbacks==0||starts>0);ActualLock(&gc_mutex,&flags,gc_argument^0x10u);
 return wait_result;
}
void Start(void*p,void*t,int32_t c,int32_t type){
 assert(p==&heap&&t==&flags&&c==8&&type==1&&!gc&&!exclusive);
 const uint32_t before=flags;assert(before==67u<<16||before==92u<<16);
 flags=74u<<16;
 ActualLock(&other_mutex,t,0xb017au);mu_proxy(&other_mutex,t); // unrelated transition locks
 ActualLock(&gc_mutex,!std::strcmp(mode,"lock-foreign-self")?&body:t,gc_argument);
 if(std::strcmp(mode,"missing-wait")){
  void* wp=!std::strcmp(mode,"bad-wait-heap")?&other_mutex:p;
  void* wt=!std::strcmp(mode,"bad-wait-self")?&body:t;
  const int32_t wc=!std::strcmp(mode,"bad-wait-cause")?9:c;
  assert(wait_proxy(wp,wc,wt)==wait_result);
 }
 // These are the actual fixed-source writes done by the original StartGC,
 // represented only in this host ART fixture; production never writes them.
 gc=true;++starts;
 if(!std::strcmp(mode,"changed-mutex")){
  mu_proxy(&other_mutex,t);MU(&gc_mutex,t);
 }else mu_proxy(&gc_mutex,t);
 assert(!exclusive&&!gc_lock);flags=before; // original C++ scope destructor
}
void Finish(void*p,void*t,int32_t type){assert(p==&heap&&t==&flags&&type==0&&gc&&!exclusive);ActualLock(&gc_mutex,t,gc_argument^0x20u);gc=false;++finishes;mu_proxy(&gc_mutex,t);}
void Stop(void*,const char*,bool){assert((flags==92u<<16||flags==74u<<16)&&!exclusive&&!gc_lock&&stops==resumes);exclusive=true;++stops;}
void Resume(void*){assert((flags==92u<<16||flags==74u<<16)&&exclusive&&!gc_lock&&stops==resumes+1);exclusive=false;++resumes;}
void Lock(void*m,void*t){assert(m==mutator&&t==&flags&&flags==92u<<16&&gc&&!exclusive);exclusive=true;}
void Unlock(void*m,void*t){assert(m==mutator&&t==&flags&&exclusive&&gc);exclusive=false;}
void Disable(void*p,void*t){assert(p==&heap&&t==&flags&&flags==67u<<16&&!exclusive);}
void Enable(void*p,void*t){assert(p==&heap&&t==&flags&&flags==67u<<16&&!exclusive);}
void CachePrepare(){}
void CacheForEach(void*,void(*)(void*,void*),void*){}
class Hooks:public HookEngine {
 public:int count=0,fail_at=0,removed=0;const char*missing=nullptr;
 bool Initialize(std::string*)override{return true;}
 template<class F>static void* Fn(F f){return reinterpret_cast<void*>(f);}
 void* FindSymbol(const char*,const char*n)override{
  if(missing&&!std::strcmp(missing,n))return nullptr;
  if(vendor&&!std::strcmp(n,"_ZN3art6Thread25ClearAllInterpreterCachesEv"))return Fn(CachePrepare);
  if(vendor&&!std::strcmp(n,"_ZZN3art6Thread25ClearAllInterpreterCachesEvE7closure"))return &other_mutex;
  if(vendor&&!std::strcmp(n,"_ZN3art5Locks17thread_list_lock_E"))return &list_mutex;
  if(vendor&&!std::strcmp(n,"_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_"))return Fn(CacheForEach);
  if(!std::strcmp(n,abi::common::kSuspendConstruct)||!std::strcmp(n,"_ZN3art16ScopedSuspendAllC1EPKcb"))return Fn(stop_proxy?stop_proxy:Stop);
  if(!std::strcmp(n,abi::common::kSuspendDestroy)||!std::strcmp(n,"_ZN3art16ScopedSuspendAllD1Ev"))return Fn(resume_proxy?resume_proxy:Resume);
  if(!std::strcmp(n,s::kStartGC))return Fn(start_proxy?start_proxy:Start);
  if(!std::strcmp(n,s::kFinishGC))return Fn(finish_proxy?finish_proxy:Finish);
  if(!std::strcmp(n,s::kCurrent))return Fn(Current);
  if(!std::strcmp(n,s::kMutator))return &mutator;
  if(!std::strcmp(n,s::kLock))return Fn(Lock);
  if(!std::strcmp(n,s::kUnlock))return Fn(Unlock);
  if(!std::strcmp(n,s::kDisableMoving))return Fn(disable_proxy?disable_proxy:Disable);
  if(!std::strcmp(n,s::kEnableMoving))return Fn(enable_proxy?enable_proxy:Enable);
  if(!std::strcmp(n,"_ZN3art2gc4Heap25WaitForGcToCompleteLockedENS0_7GcCauseEPNS_6ThreadE"))return Fn(W);
  if(!std::strcmp(n,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE"))return vendor?nullptr:Fn(ML);
  if(vendor&&!std::strcmp(n,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadEj"))return Fn(VendorML);
  if(!std::strcmp(n,"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE"))return Fn(MU);
  return nullptr;
 }
 void* Install(void*t,void*p,void**o,std::string*)override{
  assert(exclusive);++count;if(count==fail_at)return nullptr;*o=t;
  if(t==Fn(Stop))stop_proxy=reinterpret_cast<abi::common::SuspendConstruct>(p);
  else if(t==Fn(Resume))resume_proxy=reinterpret_cast<abi::common::SuspendDestroy>(p);
  else if(t==Fn(Start))start_proxy=reinterpret_cast<s::StartGC>(p);
  else if(t==Fn(Finish))finish_proxy=reinterpret_cast<s::FinishGC>(p);
  else if(t==Fn(Disable))disable_proxy=reinterpret_cast<s::MutatorOperation>(p);
  else if(t==Fn(Enable))enable_proxy=reinterpret_cast<s::MutatorOperation>(p);
  else if(t==Fn(W))wait_proxy=reinterpret_cast<Wait>(p);
  else if(t==Fn(VendorML))vendor_proxy=reinterpret_cast<VendorLock>(p);
  else if(t==Fn(ML))ml_proxy=reinterpret_cast<s::MutatorOperation>(p);
  else {assert(t==Fn(MU));mu_proxy=reinterpret_cast<s::MutatorOperation>(p);}
  return reinterpret_cast<void*>(uintptr_t(count));
 }
 bool Remove(void*t,std::string*)override{assert(exclusive);++removed;
  if(vendor){assert(uintptr_t(t)==8);wait_proxy=nullptr;}
  else switch(uintptr_t(t)){case 7:wait_proxy=nullptr;break;case 8:ml_proxy=nullptr;break;default:std::abort();}return true;}
};
bool Prepare(void*owner,native_source_stop::Session&stop)noexcept{
 assert(owner==&self_driver&&flags==74u<<16&&gc&&!gc_lock&&exclusive);
 ++callbacks;assert(stop.IsCollectedStop(&flags)&&!stop.IsFinalWindow(&flags)&&!stop.IsStopped(&body));
 if(!std::strcmp(mode,"reentry"))assert(!stop.BeginOriginalWaiting(&flags,sizeof(flags),owner,Prepare));
 if(!std::strcmp(mode,"refuse")||!std::strcmp(mode,"inspect-refuse"))return false;
 if(!std::strcmp(mode,"inspect"))return true;
 if(!std::strcmp(mode,"bad-accepted"))return true;
 return stop.ContinueSource();
}
}
int main(int argc,char**argv){
 mode=argc==2?argv[1]:"";if(!std::strncmp(mode,"oem:",4)){vendor=true;mode+=4;}Hooks hooks;std::string error;
 assert(native_source_stop::Install(hooks,&error));
 if(!std::strncmp(mode,"install-fail",12)){
  hooks.fail_at=mode[12]-'0';assert(hooks.fail_at>=7&&hooks.fail_at<=9);
  assert(!native_source_stop::InstallOriginalWaitingScope(hooks,&error));
  const int expected_removed=vendor?(hooks.fail_at==9?1:0):hooks.fail_at-7;
  assert(hooks.removed==expected_removed&&!exclusive&&stops==resumes&&!wait_proxy&&!ml_proxy&&!mu_proxy);
  if(vendor&&hooks.fail_at>7)assert(vendor_proxy);
  std::cout<<"WaitingGC installation rollback PASS\n";return 0;
 }
 if(vendor){native_cache::Service prior_cache;
  assert(prior_cache.Bind(hooks,&holder,&error)&&hooks.count==7);
 }
 assert(native_source_stop::InstallOriginalWaitingScope(hooks,&error));
 assert(hooks.count==9); // six shared source hooks, exactly three new hooks
 if(vendor){
  const int installations=hooks.count;
  assert(!native_cache::ObserveVendorMutexReturns(hooks,nullptr,&error));
  auto other_consumer=[](void*,void*,uint32_t)noexcept{};
  assert(!native_cache::ObserveVendorMutexReturns(hooks,other_consumer,&error)&&hooks.count==installations);
 }
 thread_suspend_service::Service state;
 assert(thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kLegacy16,[&](const char*n){return hooks.FindSymbol("libart.so",n);},&state)&&!state.CanTransitionRunnable());
 native_source_stop::Service service;
 assert(native_source_stop::Service::Bind(hooks,&heap,state,&service,&error));flags=67u<<16;
 if(!std::strcmp(mode,"missing-wait-calibration"))mode="missing-wait";
 if(!std::strcmp(mode,"missing-wait")||!std::strncmp(mode,"bad-wait-",9)||!std::strcmp(mode,"lock-foreign-self")){
  assert(!service.CalibrateOriginalWaiting(&flags,sizeof(flags),&error)&&!service.OriginalWaitingCalibrated()&&!gc&&!exclusive&&starts==finishes);
  std::cout<<"missing actual scope shape safely refused PASS\n";return 0;
 }
 assert(!service.CalibrateOriginalWaiting(&flags,0,&error)&&starts==0);
 const char* requested_mode=mode;mode="";
 wait_result=2;assert(service.CalibrateOriginalWaiting(&flags,sizeof(flags),&error));
 assert(service.OriginalWaitingCalibrated()&&!gc&&!exclusive&&starts==finishes&&flags==67u<<16);
 native_source_stop::Calibration proof;
 {native_source_stop::Observation observe(service,&self_driver,&body,&flags,sizeof(flags));
  assert(observe.Collected(&self_driver,&holder));disable_proxy(&heap,&flags);Native();
  unsigned char final;stop_proxy(&final,s::kFinalCause,true);resume_proxy(&final);Runnable();enable_proxy(&heap,&flags);
  assert(observe.Finish(&self_driver,true,&proof));}
 assert(service.Authorize(proof,&body)&&service.Calibrated()&&!state.CanTransitionRunnable());
 // Actual production Bridge entry executes independent full-return calibration
 // before passive original Run observation, then seals the full source lifecycle.
 native_source_stop::Service fresh;
 assert(native_source_stop::Service::Bind(hooks,&heap,state,&fresh,&error));
 if(!std::strcmp(requested_mode,"bridge-missing-wait"))mode="missing-wait";
 {native_source_calibration::Bridge bridge(fresh,&flags,sizeof(flags));
  const auto observer=native_owned_removal::waiting_fixture_observer;
  observer.begin(&self_driver,&body,&flags,observer.state);
  assert(!gc&&!exclusive&&flags==67u<<16);
  observer.collected(&self_driver,&holder,observer.state);
  disable_proxy(&heap,&flags);Native();unsigned char final;
  stop_proxy(&final,s::kFinalCause,true);resume_proxy(&final);Runnable();enable_proxy(&heap,&flags);
  observer.end(&self_driver,JVMTI_ERROR_NONE,true,observer.state);
  if(!std::strcmp(requested_mode,"bridge-missing-wait")){
   assert(!bridge.Authorized()&&!bridge.Binding().OriginalWaitingCalibrated());
   std::cout<<"Bridge true ordinary source rejects missing WaitingGC shape PASS\n";return 0;
  }
  assert(bridge.Authorized()&&bridge.Binding().OriginalWaitingCalibrated());fresh=bridge.Binding();
 }
 service=fresh;mode=requested_mode;
 if(!std::strcmp(mode,"changed-argument"))gc_argument^=0x01u;
 for(int i=0;i<3;++i){
  native_source_stop::Session stop(service);assert(!stop.Begin(&flags,sizeof(flags)));
  callbacks=0;noalloc=true;
  assert(!stop.BeginOriginalWaiting(&body,sizeof(body),&self_driver,Prepare));
  assert(!stop.BeginOriginalWaiting(&flags,0,&self_driver,Prepare));
  const bool inspection=!std::strncmp(mode,"inspect",7);
  const bool accepted=inspection?stop.InspectOriginalWaiting(&flags,sizeof(flags),&self_driver,Prepare):stop.BeginOriginalWaiting(&flags,sizeof(flags),&self_driver,Prepare);
  noalloc=false;
  assert(flags==67u<<16&&!exclusive&&!gc_lock);
  if(inspection){assert(accepted==!std::strcmp(mode,"inspect")&&callbacks==1&&!stop.Reserved()&&!stop.IsStopped(&flags)&&!gc&&starts==finishes&&stops==resumes);continue;}
  if(!std::strcmp(mode,"refuse")||!std::strcmp(mode,"changed-mutex")||!std::strcmp(mode,"changed-argument")){
   assert(!accepted&&!stop.Reserved()&&!gc&&starts==finishes&&stops==resumes);
   assert(callbacks==(!std::strcmp(mode,"refuse")?1:0));continue;
  }
  assert(accepted&&callbacks==1&&gc&&stop.Reserved()&&!stop.IsStopped(&flags));
  if(!std::strcmp(mode,"invalid-continuing-exit"))continue;
  noalloc=true;disable_proxy(&heap,&flags);Native();unsigned char final;
  stop_proxy(&final,s::kFinalCause,true);assert(stop.IsFinalWindow(&flags));
  resume_proxy(&final);Runnable();enable_proxy(&heap,&flags);noalloc=false;
  assert(stop.Completed()&&!gc&&!exclusive&&starts==finishes&&stops==resumes);
 }
 if(vendor)assert(vendor_original_calls>0);
 std::cout<<"real WaitingGC scoped stop/full return/continuous final transfer/refund repeat PASS\n";
}
