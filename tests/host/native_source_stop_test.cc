#include <cstdlib>
#include <new>
static bool forbid_native_allocation=false;
void* operator new(std::size_t bytes){if(forbid_native_allocation)throw std::bad_alloc();if(auto p=std::malloc(bytes))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete[](void* p) noexcept {::operator delete(p);}
#include "platform/common/native_source_calibration_bridge.h"
#include "platform/abi/arm64/common/redefiner_prefix.h"
#include "platform/abi/arm64/common/class_redefinition_prefix.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace artdcevm::native_owned_removal {
thread_local PassiveObserver fixture_observer;
bool ReadPassiveTrace(PassiveTrace* out) noexcept {if(!out)return false;*out={};out->events.Record(1);out->events.Record(2);out->events.Record(3);out->events.Record(4);out->original_calls=1;out->whole_collections=1;return true;}
bool SetPassiveObserver(const PassiveObserver* observer) noexcept {
 if(!observer){fixture_observer={};return true;}
 if(fixture_observer.begin)return false;
 fixture_observer=*observer;return true;
}
}

namespace artdcevm::native_hierarchy_private_ledger::detail {
// Same synchronous original-call bracket as the unique production Ledger proxy.
struct Hooks {template<class F>static void Invoke(F original,void* linker,void* cache,void* loader){
 native_source_gc_publication::Scope scope(linker,cache,loader);
 original(linker,cache,loader);scope.Returned();
}};
}
namespace {
namespace a=abi::arm64::android_common;namespace s=a::source_stop;
struct Thread {uint32_t flags=92u<<24;} self,foreign;
int heap,mutex;void* mutator=&mutex;
bool exclusive=false,gc=false,bad_transition=false,foreign_current=false;
int stops=0,resumes=0,locks=0,unlocks=0,gc_starts=0,gc_finishes=0;
abi::common::SuspendConstruct stop_proxy=nullptr;abi::common::SuspendDestroy resume_proxy=nullptr;
s::MutatorOperation disable_proxy=nullptr,enable_proxy=nullptr;
s::StartGC start_proxy=nullptr;s::FinishGC finish_proxy=nullptr;
int disable_count=0;unsigned shift=24;uint32_t runnable_state=0;
void DisableMoving(void* p,void* thread){assert(p==&heap&&thread==&self&&self.flags==(runnable_state<<shift)&&!exclusive&&disable_count==0);++disable_count;}
void EnableMoving(void* p,void* thread){assert(p==&heap&&thread==&self&&self.flags==(runnable_state<<shift)&&!exclusive&&disable_count==1);--disable_count;}
void* Current(){return foreign_current?&foreign:&self;}
void Native(void* p){assert(p==&self&&self.flags==(runnable_state<<shift)&&!exclusive);if(!bad_transition)self.flags=92u<<shift;}
void Runnable(void* p){assert(p==&self&&self.flags==92u<<shift&&!exclusive);self.flags=runnable_state<<shift;}
void Start(void* p,void* thread,int32_t cause,int32_t collector){assert(p==&heap&&thread==&self&&!gc&&!exclusive);
 assert((cause==8&&collector==1&&self.flags==92u<<shift)||(cause==12&&(collector==11||collector==13)&&self.flags==(runnable_state<<shift)));
 gc=true;++gc_starts;}
void Finish(void* p,void* thread,int32_t type){assert(p==&heap&&thread==&self&&type==0&&gc&&!exclusive);gc=false;++gc_finishes;}
void Stop(void*,const char*,bool){assert(self.flags==92u<<shift&&!exclusive&&stops==resumes);exclusive=true;++stops;}
native_source_stop::Session* inspected_original=nullptr;
void Resume(void*){if(inspected_original)assert(!inspected_original->IsFinalWindow(&self));assert(self.flags==92u<<shift&&exclusive&&stops==resumes+1);exclusive=false;++resumes;}
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
  if(!std::strcmp(name,abi::common::kSuspendConstruct)||!std::strcmp(name,"_ZN3art16ScopedSuspendAllC1EPKcb"))return Fn(stop_proxy?stop_proxy:Stop);
  if(!std::strcmp(name,abi::common::kSuspendDestroy)||!std::strcmp(name,"_ZN3art16ScopedSuspendAllD1Ev"))return Fn(resume_proxy?resume_proxy:Resume);
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
  if(target==Fn(Stop))stop_proxy=reinterpret_cast<abi::common::SuspendConstruct>(proxy);
  else if(target==Fn(Resume))resume_proxy=reinterpret_cast<abi::common::SuspendDestroy>(proxy);
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
using Callback=void(*)(void*,void*);
Callback add_callback=nullptr,remove_callback=nullptr;
void* pauser_table[4]{};int runtime_receiver,callbacks_receiver;
void AddCallback(void* c,void*){assert(c==&callbacks_receiver&&exclusive);}
void RemoveCallback(void* c,void*){assert(c==&callbacks_receiver&&exclusive);}
class LoadingHooks:public Hooks {
 public:
 void* FindSymbol(const char* lib,const char* name)override{
  if(!std::strcmp(name,"_ZTVN12openjdkjvmti21ClassDefinitionPauserE"))return pauser_table;
  if(!std::strcmp(name,"_ZN3art16RuntimeCallbacks20AddClassLoadCallbackEPNS_17ClassLoadCallbackE"))return Fn(AddCallback);
  if(!std::strcmp(name,"_ZN3art16RuntimeCallbacks23RemoveClassLoadCallbackEPNS_17ClassLoadCallbackE"))return Fn(RemoveCallback);
  return Hooks::FindSymbol(lib,name);
 }
 void* Install(void* target,void* proxy,void** original,std::string* error)override{
  if(target==Fn(AddCallback)||target==Fn(RemoveCallback)){
   assert(exclusive);*original=target;if(target==Fn(AddCallback))add_callback=reinterpret_cast<Callback>(proxy);else remove_callback=reinterpret_cast<Callback>(proxy);return target;
  }
  return Hooks::Install(target,proxy,original,error);
 }
};
struct LifecycleProof {
 void* driver=nullptr;void* self=nullptr;JNIEnv* env=nullptr;void* vm=nullptr;
 unsigned begins=0,ends=0;bool active=false,success=false;
 static void Begin(void* driver,const runnable_jni::Context& context,void* raw) noexcept {
  auto& p=*static_cast<LifecycleProof*>(raw);assert(!p.active&&p.driver==driver&&p.self==context.self&&p.env==context.env&&p.vm==context.vm);
  assert(native_source_class_loading::Run::Current()&&native_source_class_loading::Run::Current()->OwnsDriver(driver));
  p.active=true;++p.begins;
 }
 static void End(void* driver,bool success,void* raw) noexcept {
  auto& p=*static_cast<LifecycleProof*>(raw);assert(p.active&&p.driver==driver);p.active=false;p.success=success;++p.ends;
 }
};
void CleanupProofs(native_source_stop::Service& service){
 LoadingHooks hooks;std::string error;native_source_class_loading::Service loading;
 runtime_services::Snapshot runtime{};runtime.runtime=&runtime_receiver;
 assert(native_source_class_loading::Service::Bind(hooks,runtime,&loading,&error));
 Native(&self);Stop(nullptr,nullptr,false);assert(native_source_class_loading::Install(hooks,loading,&error));Resume(nullptr);Runnable(&self);
 forbid_native_allocation=true;
 for(int mode=0;mode<10;++mode){
  abi::arm64::android_common::RedefinerPrefix driver{&runtime_receiver,JVMTI_ERROR_INTERNAL,0,&runtime_receiver,&self,0};
  abi::arm64::android_common::ClassRedefinitionPrefix owner{};owner.driver=&driver;
  transaction_root::Owners owners{reinterpret_cast<unsigned char*>(&owner),reinterpret_cast<unsigned char*>(&owner)+sizeof(owner),reinterpret_cast<unsigned char*>(&owner)+sizeof(owner)};
  uint32_t handle=0;transaction_root::Holder holder{};holder.array_handle=&handle;holder.redefinitions=&owners;
  runnable_jni::Context context{&self,reinterpret_cast<JNIEnv*>(&runtime_receiver),&runtime_receiver};
  native_source_class_loading::Run run(loading,&driver,context);
  native_source_stop::Observation o(service,&driver,reinterpret_cast<const void*>(&CleanupProofs),&self,sizeof(self));assert(o.BindCleanup(run));
  struct Pauser {void*table;void*self;uint8_t running;}pauser{pauser_table+2,&self,1};
  Native(&self);unsigned char initial;stop_proxy(&initial,"constructor",false);add_callback(&callbacks_receiver,&pauser);resume_proxy(&initial);Runnable(&self);
  transaction_root::Iterator iterator{0,&holder};assert(!run.Paused());assert(run.BeforeFinish(&owner,&holder,&iterator));assert(run.Paused());run.AfterFinish(&owner,true);assert(run.Paused());
  assert(o.Collected(&driver,&holder));disable_proxy(&heap,&self);Native(&self);unsigned char final,cleanup,foreign_carrier;
  stop_proxy(&final,"actual final",true);resume_proxy(&final);Runnable(&self);enable_proxy(&heap,&self);
  if(mode==6){Native(&self);start_proxy(&heap,&self,8,1);finish_proxy(&heap,&self,0);Runnable(&self);}
  if(mode==1)pauser.self=&foreign;
  if(mode==7)pauser.table=nullptr;
  if(mode==9){Native(&self);start_proxy(&heap,&self,8,1);Runnable(&self);}
  Native(&self);stop_proxy(&cleanup,"arbitrary cause does not grant permission",false);
  remove_callback(&callbacks_receiver,&pauser);
  resume_proxy(mode==2?&foreign_carrier:&cleanup);Runnable(&self);
  if(mode==3||mode==4){Native(&self);start_proxy(&heap,&self,8,1);if(mode==3)finish_proxy(&heap,&self,0);Runnable(&self);}
  if(mode==8){Native(&self);stop_proxy(&foreign_carrier,"second cleanup",false);resume_proxy(&foreign_carrier);Runnable(&self);}
  bool original=mode!=5;bool complete=run.Complete(original);native_source_stop::Calibration proof;
  bool valid=o.Finish(&driver,original&&complete,&proof);
  assert(valid==(mode==0||mode==3||mode==6));
  if(mode==4||mode==9){Native(&self);finish_proxy(&heap,&self,0);Runnable(&self);}
 }
 for(int mode=0;mode<4;++mode){
  abi::arm64::android_common::RedefinerPrefix driver{&runtime_receiver,JVMTI_ERROR_INTERNAL,0,&runtime_receiver,&self,0};
  runnable_jni::Context context{&self,reinterpret_cast<JNIEnv*>(&runtime_receiver),&runtime_receiver};
  LifecycleProof lifecycle{&driver,context.self,context.env,context.vm};
  {
   native_source_calibration::Bridge bridge(service,&self,sizeof(self),loading,context,{&lifecycle,LifecycleProof::Begin,LifecycleProof::End});
   auto observer=native_owned_removal::fixture_observer;
   observer.begin(&driver,reinterpret_cast<const void*>(&CleanupProofs),mode==2?&foreign:&self,observer.state);
   assert(lifecycle.begins==(mode==2?0u:1u));
   if(mode!=3)observer.end(&driver,mode==1?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE,mode==0,observer.state);
   assert(!bridge.Authorized()); // No collection/final/native cleanup was supplied.
  }
  assert(!lifecycle.active&&lifecycle.ends==lifecycle.begins);
  assert(lifecycle.success==(mode==0));
  assert(!native_owned_removal::fixture_observer.begin&&!native_source_class_loading::Run::Current());
 }
 forbid_native_allocation=false;
}
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

int publication_linker,publication_driver,publication_holder,publication_caches[2],publication_loaders[2];
const char* publication_mode="";bool publication_has_gc=true;int publication_collector=11,no_suspend_depth=0,publication_original_calls=0;
void PublishOriginal(void* linker,void* cache,void* loader){
 assert(linker==&publication_linker&&cache&&loader&&self.flags==(runnable_state<<shift)&&!exclusive);
 ++publication_original_calls;
 if(publication_has_gc){
  if(!std::strcmp(publication_mode,"bad-cause"))start_proxy(&heap,&self,8,publication_collector);
  else if(!std::strcmp(publication_mode,"bad-collector"))start_proxy(&heap,&self,12,publication_collector==11?13:11);
  else if(!std::strcmp(publication_mode,"bad-heap"))start_proxy(&mutex,&self,12,publication_collector);
  else if(!std::strcmp(publication_mode,"bad-gc-thread"))start_proxy(&heap,&foreign,12,publication_collector);
  else start_proxy(&heap,&self,12,publication_collector);
  // These original ART no-thread-suspension operations remain around the
  // borrowed exclusion; they must never be replaced by the GC hook.
  ++no_suspend_depth;assert(no_suspend_depth==1);--no_suspend_depth;
  if(!std::strcmp(publication_mode,"missing-finish"))return;
  finish_proxy(&heap,&self,!std::strcmp(publication_mode,"bad-finish")?1:0);
  if(!std::strcmp(publication_mode,"calibration-double")){start_proxy(&heap,&self,12,publication_collector);finish_proxy(&heap,&self,0);}
 }
}
struct PublicationOwner {
 native_source_stop::Session* session;bool deny=false;size_t checks=0;
 static bool Match(void* raw,void* driver,const void* holder,void* linker,void* cache,void* loader,size_t* index) noexcept {
  auto& p=*static_cast<PublicationOwner*>(raw);++p.checks;
  if(p.deny||!index||driver!=&publication_driver||holder!=&publication_holder||linker!=&publication_linker||
     !p.session->CanPublishDex(driver,holder,linker))return false;
  for(size_t i=0;i<2;++i)if(cache==&publication_caches[i]&&loader==&publication_loaders[i]){*index=i;return true;}
  return false;
 }
};
void PublicationProofs(const native_source_stop::Service& base,const char* mode){
 using Proxy=native_hierarchy_private_ledger::detail::Hooks;
 publication_mode="";publication_has_gc=std::strcmp(mode,"no-gc-profile")!=0&&std::strcmp(mode,"unexpected-gc-profile")!=0;
 publication_collector=!std::strcmp(mode,"collector13")?13:11;
 auto service=base;int run;native_source_stop::Calibration proof;
 {native_source_stop::Session missing(base);PublicationOwner tuple{&missing};std::string error;
  assert(!missing.PrepareDexPublication(&publication_driver,&publication_holder,&publication_linker,2,{&tuple,PublicationOwner::Match},&error));}
 if(!std::strcmp(mode,"calibration-double"))publication_mode=mode;
 {native_source_stop::Observation observed(service,&publication_driver,&run,&self,sizeof(self));
  assert(observed.Collected(&publication_driver,&publication_holder));
  for(size_t i=0;i<2;++i){Proxy::Invoke(PublishOriginal,&publication_linker,&publication_caches[i],&publication_loaders[i]);
   if(!std::strcmp(mode,"calibration-mixed"))publication_has_gc=false;}
  disable_proxy(&heap,&self);Native(&self);unsigned char final;
  stop_proxy(&final,s::kFinalCause,true);resume_proxy(&final);Runnable(&self);enable_proxy(&heap,&self);
  if(!std::strcmp(mode,"calibration-double")||!std::strcmp(mode,"calibration-mixed")){
   assert(!observed.Finish(&publication_driver,true,&proof)&&!proof.Valid()&&!service.DexPublicationCalibrated());
   std::cout<<"unpaired/mixed observed publication cannot create calibration PASS\n";return;}
  assert(observed.Finish(&publication_driver,true,&proof)&&service.Authorize(proof,&run)&&service.DexPublicationCalibrated());
 }
 {native_source_stop::Session local(service);assert(local.Begin(&self,sizeof(self))&&local.IsCollectedStop(&self));
  assert(!local.ContinueSource()&&local.RollbackBeforeSource()&&!local.Reserved());}
 native_source_stop::Session owner(service);PublicationOwner tuple{&owner};std::string error;
 assert(!owner.PrepareDexPublication(nullptr,&publication_holder,&publication_linker,2,{&tuple,PublicationOwner::Match},&error));
 assert(!owner.PrepareDexPublication(&publication_driver,&publication_holder,&mutex,2,{&tuple,PublicationOwner::Match},&error));
 assert(owner.PrepareDexPublication(&publication_driver,&publication_holder,&publication_linker,2,{&tuple,PublicationOwner::Match},&error));
 assert(!owner.CanPublishDex(&publication_driver,&publication_holder,&publication_linker));
 assert(owner.Begin(&self,sizeof(self)));
 if(!std::strcmp(mode,"bad-phase"))Proxy::Invoke(PublishOriginal,&publication_linker,&publication_caches[0],&publication_loaders[0]);
 assert(owner.ContinueSource()&&owner.IsPublicationWindow(&self)&&!owner.IsPublicationWindow(&foreign));
 assert(!owner.CanPublishDex(&run,&publication_holder,&publication_linker)&&!owner.CanPublishDex(&publication_driver,&run,&publication_linker));
 publication_mode=mode;if(!std::strcmp(mode,"unexpected-gc-profile"))publication_has_gc=true;const auto real_starts=gc_starts,real_finishes=gc_finishes;
 forbid_native_allocation=true;
 if(!std::strcmp(mode,"outside-callee"))start_proxy(&heap,&self,12,publication_collector);
 if(!std::strcmp(mode,"foreign-current"))foreign_current=true;
 if(!std::strcmp(mode,"bad-owner"))tuple.deny=true;
 for(size_t i=0;i<2;++i){
  Proxy::Invoke(PublishOriginal,!std::strcmp(mode,"bad-linker")?&mutex:&publication_linker,
      !std::strcmp(mode,"bad-cache")?&mutex:&publication_caches[i],
      !std::strcmp(mode,"bad-loader")?&mutex:&publication_loaders[i]);
  if(!std::strcmp(mode,"duplicate"))Proxy::Invoke(PublishOriginal,&publication_linker,&publication_caches[i],&publication_loaders[i]);
  if(!std::strcmp(mode,"missing-publication"))break;
 }
 assert(gc_starts==real_starts&&gc_finishes==real_finishes&&gc&&owner.Reserved()&&tuple.checks>=(!std::strcmp(mode,"missing-publication")?2u:4u)&&no_suspend_depth==0);
 disable_proxy(&heap,&self);Native(&self);unsigned char final;
 stop_proxy(&final,s::kFinalCause,true);resume_proxy(&final);Runnable(&self);enable_proxy(&heap,&self);
 forbid_native_allocation=false;
 assert(owner.Completed()&&!owner.Reserved()&&!owner.IsPublicationWindow(&self)&&!gc&&!exclusive&&gc_starts==gc_finishes);
 publication_mode="";std::cout<<"named source publication exact tuple/observed GC profile, paired stronger reservation borrowing, preserved no-suspend and final cleanup PASS\n";
}

void BridgeProofs(const native_source_stop::Service& service){
 int driver,run,holder;
 forbid_native_allocation=true;
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
  disable_proxy(&heap,&self);
  if(mode==4){Native(&self);start_proxy(&heap,&self,8,1);finish_proxy(&heap,&self,0);Runnable(&self);}
  Native(&self);unsigned char carrier,wrong_carrier;
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

void TraceRegressions(native_source_stop::Service& service){
 int driver,run,holder,wrong_driver;
 self.flags=92u<<shift;
 forbid_native_allocation=true;
 {native_source_stop::Observation failed(service,&driver,&run,&self,sizeof(self));assert(!failed.Ready());
  assert(!failed.Collected(&wrong_driver,&holder));native_source_stop::Calibration empty;
  assert(!failed.Finish(&driver,true,&empty));
  assert(!std::strcmp(failed.Failure(),"ordinary-source-observation-owner-mismatch"));
  const auto& t=failed.Trace();assert(t.states[0]==92&&t.states[1]==92&&t.states[7]==92&&t.failed&&t.counts[0]==1&&t.counts[1]==1&&t.counts[7]==1&&t.events.order==0x821&&t.first_failure_phase==0);
 }
 self.flags=runnable_state<<shift;
 {native_source_stop::Observation o(service,&driver,&run,&self,sizeof(self));assert(o.Collected(&driver,&holder));
  Native(&self);start_proxy(&heap,&self,8,1);finish_proxy(&heap,&self,0);Runnable(&self);
  disable_proxy(&heap,&self);Native(&self);unsigned char final;
  stop_proxy(&final,"actual final after prefinal GC",true);resume_proxy(&final);Runnable(&self);enable_proxy(&heap,&self);
  native_source_stop::Calibration proof;assert(o.Finish(&driver,true,&proof)&&proof.Valid());
  assert(o.Trace().counts[6]==2&&!o.Trace().failed);
 }
 {native_source_stop::Observation o(service,&driver,&run,&self,sizeof(self));assert(o.Collected(&driver,&holder));
  Native(&self);start_proxy(&heap,&self,8,1);Runnable(&self);
  // A start with no matching finish cannot arm final exclusion. Clean the
  // fixture reservation only after recording that rejection.
  disable_proxy(&heap,&self);Native(&self);finish_proxy(&heap,&self,0);unsigned char final;
  stop_proxy(&final,"unfinished prefinal GC",true);resume_proxy(&final);Runnable(&self);enable_proxy(&heap,&self);
  native_source_stop::Calibration proof;assert(!o.Finish(&driver,true,&proof)&&!proof.Valid());
  assert(!std::strcmp(o.Failure(),"ordinary-source-original-GC-disable-owner-order-mismatch"));
 }
 {native_source_calibration::Bridge bridge(service,&self,sizeof(self));auto o=native_owned_removal::fixture_observer;
  o.begin(&driver,&run,&self,o.state);self.flags=92u<<shift;o.collected(&driver,&holder,o.state);self.flags=runnable_state<<shift;
  o.end(&driver,JVMTI_ERROR_NONE,true,o.state);
  // Observation was destroyed by End, but both its first reason and scalar
  // evidence remain owned by Bridge, without heap allocation or native roots.
  assert(!bridge.Authorized()&&std::strstr(bridge.Failure(),"ordinary-source-Collected-owner-order-mismatch")&&std::strstr(bridge.Failure(),"states=0/92/0")&&std::strstr(bridge.Failure(),"passive:available=1")&&std::strlen(bridge.Failure())<768);
 }
 {native_source_stop::Observation o(service,&driver,&run,&self,sizeof(self));assert(o.Collected(&driver,&holder));
  disable_proxy(&heap,&self);Native(&self);unsigned char final,cleanup;
  stop_proxy(&final,"actual final",true);resume_proxy(&final);Runnable(&self);enable_proxy(&heap,&self);
  // Class-loading cleanup is a real extra Native suspend after GC enabled.
  // Record its phase and retain strict refusal, rather than silently ignore it.
  Native(&self);stop_proxy(&cleanup,"class loading cleanup",false);resume_proxy(&cleanup);Runnable(&self);
  native_source_stop::Calibration proof;assert(!o.Finish(&driver,true,&proof));
  assert(!std::strcmp(o.Failure(),"ordinary-source-original-final-stop-owner-order-mismatch"));
  const auto& t=o.Trace();assert(t.counts[3]==2&&t.counts[4]==2&&t.phase_masks[3]==((1u<<2)|(1u<<5))&&t.first_failure_phase==5&&!proof.Valid());
 }
 BoundedNativeEventTrace bounded;for(unsigned n=0;n<30;++n)bounded.Record(1);
 assert(bounded.events==30&&bounded.truncated&&bounded.order==0x1111111111111111ULL);
 forbid_native_allocation=false;
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
 assert(service.AuthorizeOriginalFinal(proof,&run)&&service.OriginalFinalCalibrated());
 const int original_starts=gc_starts,original_locks=locks,original_unlocks=unlocks;
 forbid_native_allocation=true;
 for(int mode=0;mode<6;++mode){
  native_source_stop::Session borrowed(service);
  if(mode==1){int wrong;assert(!borrowed.PrepareOriginalFinal(&self,sizeof(self),&wrong,&driver));continue;}
  assert(borrowed.PrepareOriginalFinal(&self,sizeof(self),&run,&driver));
  assert(borrowed.BorrowsOriginalFinal()&&!borrowed.IsFinalWindow(&self)&&!borrowed.Reserved());
  assert(!borrowed.Begin(&self,sizeof(self))&&!borrowed.ContinueSource());
  if(mode==2){int wrong;assert(!borrowed.OriginalCollected(&wrong,&holder));}
  else assert(borrowed.OriginalCollected(&driver,&holder));
  if(mode==5)assert(!borrowed.OriginalCollected(&driver,&holder));
  disable_proxy(&heap,&self);Native(&self);unsigned char actual_carrier;
  stop_proxy(&actual_carrier,mode==3?"unrelated stop":s::kFinalCause,true);
  const bool accepted=mode==0||mode==4;
  assert(borrowed.IsFinalWindow(&self)==accepted&&borrowed.IsStopped(&self)==accepted);
  assert(!borrowed.IsCollectedStop(&self)&&!borrowed.Reserved());
  if(mode==4){
   foreign_current=true;assert(!borrowed.IsFinalWindow(&self));foreign_current=false;
   int wrong_mutator;mutator=&wrong_mutator;assert(!borrowed.IsFinalWindow(&self));mutator=&mutex;
   self.flags=67u<<16;assert(!borrowed.IsFinalWindow(&self));self.flags=92u<<16;
   assert(borrowed.IsFinalWindow(&self));
  }
  inspected_original=&borrowed;resume_proxy(&actual_carrier);inspected_original=nullptr;
  assert(!borrowed.IsFinalWindow(&self)&&!borrowed.Completed());
  Runnable(&self);enable_proxy(&heap,&self);
  assert(borrowed.Completed()==accepted);
 }
 forbid_native_allocation=false;
 assert(gc_starts==original_starts&&locks==original_locks&&unlocks==original_unlocks);
 assert(!service.AuthorizeOriginalFinal(proof,nullptr)&&!service.OriginalFinalCalibrated());

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
 TraceRegressions(service);
 LegacyObservation(hooks);
 ObservationNegatives(service,hooks);
 CleanupProofs(service);
 assert(!service.Calibrated());
 self.flags=runnable_state<<shift;{native_source_stop::Session uncalibrated(service);assert(!uncalibrated.Begin(&self,sizeof(self))&&!gc&&!exclusive);}
 Calibrate(service);
 {int other_mutator;mutator=&other_mutator;native_source_stop::Session changed(service);assert(!changed.Begin(&self,sizeof(self))&&self.flags==(runnable_state<<shift)&&!gc&&!exclusive);mutator=&mutex;}
 self.flags=92u<<shift;
 // No callback/phase boolean alone can grant any stop permission.
 native_source_stop::Session unopened(service);assert(unopened.RollbackBeforeSource());assert(!unopened.IsStopped(&self)&&!unopened.Reserved()&&!unopened.IsCollectedStop(&self)&&!unopened.IsFinalWindow(&self));
 assert(!unopened.Begin(&self,sizeof(self))); // ordinary Native JNI is not source Runnable
 assert(!unopened.Begin(&foreign,sizeof(foreign)));
 self.flags=runnable_state<<shift;
 {native_source_stop::Session owner(service);assert(!owner.Begin(&self,0)&&!gc&&!exclusive);}
 bad_transition=true;
 {native_source_stop::Session owner(service);assert(!owner.Begin(&self,sizeof(self))&&!gc&&!exclusive&&self.flags==(runnable_state<<shift));}
 bad_transition=false;
 // Generic scoped inspection uses the actual calibrated Native pair when
 // present. Success and refusal both restore Runnable and release all rights.
 for(bool accept:{false,true}){
  struct Request{bool accept;unsigned calls=0;};Request request{accept};
  native_source_stop::Session inspection(service);
  const auto operation=[](void* raw,native_source_stop::Session& scope)noexcept{
   auto& request=*static_cast<Request*>(raw);++request.calls;
   assert(scope.IsCollectedStop(&self)&&scope.IsStopped(&self)&&gc&&exclusive);
   assert(!scope.IsFinalWindow(&self));return request.accept;
  };
  const int starts=gc_starts;
  assert(inspection.Inspect(&self,sizeof(self),&request,operation)==accept);
  assert(request.calls==1&&gc_starts==starts+1&&!gc&&!exclusive&&self.flags==(runnable_state<<shift));
  assert(!inspection.Reserved()&&!inspection.IsStopped(&self));
 }
 // A real refundable scan refusal unwinds the actual reservation, not just flags.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self))&&owner.IsStopped(&self)&&owner.IsCollectedStop(&self)&&!owner.IsFinalWindow(&self)&&gc&&exclusive);
  assert(!owner.IsStopped(&foreign)&&!owner.IsCollectedStop(&foreign)&&!owner.IsFinalWindow(&foreign));foreign_current=true;assert(!owner.IsStopped(&self));foreign_current=false;
  native_source_stop::Session nested(service);const int count=gc_starts;assert(!nested.Begin(&self,sizeof(self))&&gc_starts==count);
  assert(owner.RollbackBeforeSource()&&!owner.IsStopped(&self)&&!owner.Reserved()&&!gc&&!exclusive&&self.flags==(runnable_state<<shift));assert(owner.RollbackBeforeSource());
 }
 assert(!gc&&!exclusive&&self.flags==(runnable_state<<shift)&&stops==resumes);
 // Original source continuation preserves actual global suspension and GC.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self)));
  assert(owner.IsCollectedStop(&self)&&!owner.IsFinalWindow(&self));
  const int captured=stops;assert(owner.ContinueSource()&&gc&&!exclusive&&owner.Reserved()&&!owner.IsStopped(&self)&&self.flags==(runnable_state<<shift)&&stops==captured&&stops==resumes+1);
  assert(!owner.IsCollectedStop(&self)&&!owner.IsFinalWindow(&self));
  if(argc==2&&!std::strcmp(argv[1],"wrong-final")){disable_proxy(&heap,&self);Native(&self);unsigned char wrong;stop_proxy(&wrong,"unrelated stop",true);assert(false);}
  disable_proxy(&heap,&self);Native(&self);unsigned char original_source_carrier;
  stop_proxy(&original_source_carrier,s::kFinalCause,true);
  assert(owner.IsStopped(&self)&&!owner.IsCollectedStop(&self)&&owner.IsFinalWindow(&self)&&stops==captured&&gc&&exclusive);
  resume_proxy(&original_source_carrier);
  assert(!owner.Completed()&&!owner.IsCollectedStop(&self)&&!owner.IsFinalWindow(&self)&&!gc&&!exclusive&&stops==resumes&&self.flags==92u<<shift);
  Runnable(&self);enable_proxy(&heap,&self);assert(owner.Completed()&&!owner.IsCollectedStop(&self)&&!owner.IsFinalWindow(&self)&&disable_count==0);
 }
 // Abnormal source return still releases the owned reservation from Runnable.
 {native_source_stop::Session owner(service);assert(owner.Begin(&self,sizeof(self))&&owner.ContinueSource());assert(!owner.RollbackBeforeSource());}
 assert(!gc&&!exclusive&&self.flags==(runnable_state<<shift)&&stops==resumes&&gc_starts==gc_finishes&&locks==unlocks);
 PublicationProofs(service,argc==2?argv[1]:"");
 std::cout<<"continuous original stop/GC ownership, transfer and rollback PASS\n";return 0;
}
