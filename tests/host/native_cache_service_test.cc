#include "platform/common/native_cache_service.h"
#include "platform/abi/arm64/android-common/closure.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace artdcevm;
namespace {
int self,threads,other_threads,mutex,worker[3];
void* list_lock=&mutex;
int stopped=0,locked=0,clears=0,checkpoint_calls=0;
bool conflict=false,throw_prepare=false,wrong_owner=false,vendor=false,vendor_wrong=false,vendor_conflict=false,foreign=false;
using VendorLock=void(*)(void*,void*,uint32_t);
VendorLock vendor_proxy=nullptr;
constexpr uint32_t observed_argument=0xaf001234u;
int vendor_calls=0;
using Checkpoint=size_t(*)(void*,void*,void*,bool,bool);
Checkpoint active_checkpoint=nullptr;
struct NativeClosure:art::Closure {
 void Run(art::Thread* thread)override{assert(stopped&&locked&&thread);++clears;}
} native_closure,other_closure;
void* Current(){return foreign?&other_threads:&self;}
void VendorOriginal(void* lock,void* thread,uint32_t argument){++vendor_calls;if(stopped&&lock==&mutex){assert(thread==&self&&argument==observed_argument&&!locked);++locked;}}
void Stop(void*,const char*,bool){assert(!stopped&&!locked);++stopped;}
void Resume(void*){assert(stopped==1&&!locked);--stopped;}
size_t Checkpoints(void*,void*,void*,bool,bool){++checkpoint_calls;return 3;}
void Prepare(){
 assert(!stopped&&!locked);
 if(throw_prepare)throw std::runtime_error("owner bootstrap");
 if(vendor){
  vendor_proxy(&other_threads,&self,11);vendor_proxy(&mutex,&other_threads,12);
  if(!vendor_wrong){vendor_proxy(&mutex,&self,observed_argument);if(vendor_conflict)vendor_proxy(&mutex,&self,observed_argument+1);}
 }
 // Calls outside the owned receiver/argument contract must be passthroughs.
 active_checkpoint(&other_threads,&other_closure,nullptr,true,false);
 active_checkpoint(&threads,&other_closure,nullptr,false,false);
 active_checkpoint(&threads,&other_closure,&self,true,false);
 if(wrong_owner)return;
 active_checkpoint(&threads,&native_closure,nullptr,true,false);
 if(conflict)active_checkpoint(&threads,&other_closure,nullptr,true,false);
}
void Lock(void* lock,void* thread){assert(stopped&&lock==&mutex&&thread==&self&&!locked);++locked;}
void Unlock(void* lock,void* thread){assert(stopped&&lock==&mutex&&thread==&self&&locked==1);--locked;}
void ForEach(void* list,void(*run)(void*,void*),void* data){
 assert(stopped&&locked&&list==&threads);
 for(auto& thread:worker)run(&thread,data);
}
class Hooks:public HookEngine {
 public:
 bool named=false,missing=false;int installs=0;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override {
  if(missing)return nullptr;
  if(!std::strcmp(name,"_ZZN3art6Thread25ClearAllInterpreterCachesEvE7closure"))return named?&native_closure:nullptr;
  if(!std::strcmp(name,"_ZN3art6Thread25ClearAllInterpreterCachesEv"))return reinterpret_cast<void*>(Prepare);
  if(!std::strcmp(name,"_ZN3art6Thread14CurrentFromGdbEv"))return reinterpret_cast<void*>(Current);
  if(!std::strcmp(name,"_ZN3art5Locks17thread_list_lock_E"))return &list_lock;
  if(!std::strcmp(name,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE"))return vendor?nullptr:reinterpret_cast<void*>(Lock);
  if(!std::strcmp(name,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadEj"))return vendor?reinterpret_cast<void*>(VendorOriginal):nullptr;
  if(!std::strcmp(name,"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE"))return reinterpret_cast<void*>(Unlock);
  if(!std::strcmp(name,"_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_"))return reinterpret_cast<void*>(ForEach);
  if(!std::strcmp(name,"_ZN3art10ThreadList13RunCheckpointEPNS_7ClosureES2_bb"))return reinterpret_cast<void*>(Checkpoints);
  if(!std::strcmp(name,abi::arm64::android_common::kSuspendConstruct))return reinterpret_cast<void*>(Stop);
  if(!std::strcmp(name,abi::arm64::android_common::kSuspendDestroy))return reinterpret_cast<void*>(Resume);
  return nullptr;
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override {
  assert(stopped);++installs;
  if(target==reinterpret_cast<void*>(VendorOriginal)){*original=target;vendor_proxy=reinterpret_cast<VendorLock>(proxy);vendor_proxy(&other_threads,&self,99);return &installs;}
  assert(target==reinterpret_cast<void*>(Checkpoints));
  *original=target;active_checkpoint=reinterpret_cast<Checkpoint>(proxy);
  // A native caller can reach the hook before Install returns: its original
  // must already be available through durable storage, not a later copy.
  assert(active_checkpoint(&other_threads,&native_closure,nullptr,true,false)==3);
  return &installs;
 }
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
void Clear(native_cache::Service& service){
 std::string error;size_t count=77;
 Stop(nullptr,nullptr,false);assert(service.ClearStoppedWorld(&count,&error));Resume(nullptr);
 assert(count==3&&!stopped&&!locked);
}
}
int main(){
 Hooks hooks;native_cache::Service service;std::string error;size_t count=77;
 assert(!service.Bind(hooks,nullptr,&error));
 assert(service.Bind(hooks,&threads,&error)&&hooks.installs==1);
 assert(!service.ClearStoppedWorld(&count,&error)&&count==77);
 wrong_owner=true;assert(!service.Prepare(&error));wrong_owner=false;
 conflict=true;assert(!service.Prepare(&error));conflict=false;
 throw_prepare=true;
 try{service.Prepare(&error);assert(false);}catch(const std::runtime_error&){}
 throw_prepare=false;
 assert(service.Prepare(&error));Clear(service);assert(clears==3);
 hooks.missing=true;assert(!service.Bind(hooks,&threads,&error));hooks.missing=false;
 Clear(service);assert(clears==6);
 native_cache::Service named;hooks.named=true;
 assert(named.Bind(hooks,&threads,&error)&&named.Prepare(&error));Clear(named);
 assert(clears==9&&hooks.installs==1&&checkpoint_calls>0&&!stopped&&!locked);
 vendor=true;native_cache::Service extension;
 assert(extension.Bind(hooks,&threads,&error)&&hooks.installs==2);
 vendor_wrong=true;assert(!extension.Prepare(&error));vendor_wrong=false;
 vendor_conflict=true;assert(!extension.Prepare(&error));vendor_conflict=false;
 throw_prepare=true;try{extension.Prepare(&error);assert(false);}catch(const std::runtime_error&){}throw_prepare=false;
 assert(extension.Prepare(&error)&&extension.Prepared()&&extension.PreparedForCurrentThread());Clear(extension);
 foreign=true;assert(!extension.PreparedForCurrentThread());count=77;Stop(nullptr,nullptr,false);assert(!extension.ClearStoppedWorld(&count,&error)&&count==77);Resume(nullptr);foreign=false;
 Clear(extension);assert(clears==15&&vendor_calls>6&&!stopped&&!locked);
 std::cout<<"Native cache: original publication, owned capture, ambiguous/missing rejection, TLS recovery, actual vendor argument/owner capture, conflicts and wrong-thread rejection, synchronous enumeration passed\n";
}
