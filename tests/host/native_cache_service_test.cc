#include "platform/common/native_cache_service.h"
#include "platform/abi/arm64/common/closure.h"
#include "platform/abi/arm64/common/suspend_all.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "platform/abi/arm64/common/checkpoint.h"
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
int shape=5;
using namespace abi::arm64::android_common;
CheckpointMutator active5=nullptr;CheckpointChecking active4=nullptr;CheckpointCallback active3=nullptr;
size_t Invoke(void* receiver,art::Closure* closure,art::Closure* callback,bool checking=true,bool acquire=false){
 if(shape==5)return active5(receiver,closure,callback,checking,acquire);
 if(shape==4)return active4(receiver,closure,callback,checking);
 return active3(receiver,closure,callback);
}
struct NativeClosure:art::Closure {
 void Run(art::Thread* thread)override{assert(stopped&&locked&&thread);++clears;}
} native_closure,other_closure;
void* Current(){return foreign?&other_threads:&self;}
void VendorOriginal(void* lock,void* thread,uint32_t argument){++vendor_calls;if(stopped&&lock==&mutex){assert(thread==&self&&argument==observed_argument&&!locked);++locked;}}
void Stop(void*,const char*,bool){assert(!stopped&&!locked);++stopped;}
void Resume(void*){assert(stopped==1&&!locked);--stopped;}
size_t Checkpoints5(void* receiver,art::Closure* closure,art::Closure* callback,bool checking,bool acquire){
 assert(receiver&&closure);if(callback)assert(callback==&other_closure);if(acquire)assert(!checking);
 ++checkpoint_calls;return 3;
}
size_t Checkpoints4(void* receiver,art::Closure* closure,art::Closure* callback,bool checking){return Checkpoints5(receiver,closure,callback,checking,false);}
size_t Checkpoints3(void* receiver,art::Closure* closure,art::Closure* callback){return Checkpoints5(receiver,closure,callback,true,false);}
void Prepare(){
 assert(!stopped&&!locked);
 if(throw_prepare)throw std::runtime_error("owner bootstrap");
 if(vendor){
  vendor_proxy(&other_threads,&self,11);vendor_proxy(&mutex,&other_threads,12);
  if(!vendor_wrong){vendor_proxy(&mutex,&self,observed_argument);if(vendor_conflict)vendor_proxy(&mutex,&self,observed_argument+1);}
 }
 // Calls outside the owned receiver/argument contract must be passthroughs.
 Invoke(&other_threads,&other_closure,nullptr,true,false);
 if(shape!=3)Invoke(&threads,&other_closure,nullptr,false,false);
 if(shape==5)Invoke(&threads,&other_closure,nullptr,false,true);
 Invoke(&threads,&other_closure,&other_closure,true,false);
 if(wrong_owner)return;
 Invoke(&threads,&native_closure,nullptr,true,false);
 if(conflict)Invoke(&threads,&other_closure,nullptr,true,false);
}
void Lock(void* lock,void* thread){assert(stopped&&lock==&mutex&&thread==&self&&!locked);++locked;}
void Unlock(void* lock,void* thread){assert(stopped&&lock==&mutex&&thread==&self&&locked==1);--locked;}
void ForEach(void* list,void(*run)(void*,void*),void* data){
 assert(stopped&&locked&&list==&threads);
 for(auto& thread:worker)run(&thread,data);
}
class Hooks:public HookEngine {
 public:
 bool named=false,missing=false,missing_checkpoint=false;int installs=0;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override {
  if(missing)return nullptr;
  if(missing_checkpoint&&(!std::strcmp(name,kCheckpointMutator)||!std::strcmp(name,kCheckpointChecking)||!std::strcmp(name,kCheckpointCallback)))return nullptr;
  if(!std::strcmp(name,"_ZZN3art6Thread25ClearAllInterpreterCachesEvE7closure"))return named?&native_closure:nullptr;
  if(!std::strcmp(name,"_ZN3art6Thread25ClearAllInterpreterCachesEv"))return reinterpret_cast<void*>(Prepare);
  if(!std::strcmp(name,"_ZN3art6Thread14CurrentFromGdbEv"))return reinterpret_cast<void*>(Current);
  if(!std::strcmp(name,"_ZN3art5Locks17thread_list_lock_E"))return &list_lock;
  if(!std::strcmp(name,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE"))return vendor?nullptr:reinterpret_cast<void*>(Lock);
  if(!std::strcmp(name,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadEj"))return vendor?reinterpret_cast<void*>(VendorOriginal):nullptr;
  if(!std::strcmp(name,"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE"))return reinterpret_cast<void*>(Unlock);
  if(!std::strcmp(name,"_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_"))return reinterpret_cast<void*>(ForEach);
  if(!std::strcmp(name,kCheckpointMutator))return shape==5?reinterpret_cast<void*>(Checkpoints5):nullptr;
  if(!std::strcmp(name,kCheckpointChecking))return shape==4?reinterpret_cast<void*>(Checkpoints4):nullptr;
  if(!std::strcmp(name,kCheckpointCallback))return shape==3?reinterpret_cast<void*>(Checkpoints3):nullptr;
  if(!std::strcmp(name,abi::arm64::android_common::kSuspendConstruct))return reinterpret_cast<void*>(Stop);
  if(!std::strcmp(name,abi::arm64::android_common::kSuspendDestroy))return reinterpret_cast<void*>(Resume);
  return nullptr;
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override {
  assert(stopped);++installs;
  if(target==reinterpret_cast<void*>(VendorOriginal)){*original=target;vendor_proxy=reinterpret_cast<VendorLock>(proxy);vendor_proxy(&other_threads,&self,99);return &installs;}
  assert(target==(shape==5?reinterpret_cast<void*>(Checkpoints5):shape==4?reinterpret_cast<void*>(Checkpoints4):reinterpret_cast<void*>(Checkpoints3)));
  *original=target;
  if(shape==5)active5=reinterpret_cast<CheckpointMutator>(proxy);
  if(shape==4)active4=reinterpret_cast<CheckpointChecking>(proxy);
  if(shape==3)active3=reinterpret_cast<CheckpointCallback>(proxy);
  // A native caller can reach the hook before Install returns: its original
  // must already be available through durable storage, not a later copy.
  assert(Invoke(&other_threads,&native_closure,nullptr,true,false)==3);
  return &installs;
 }
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
struct Scan {void* expected[3];size_t visited=0;bool refuse=false;};
bool ReadThread(void* thread,void* raw){
 assert(stopped&&locked);auto& scan=*static_cast<Scan*>(raw);
 assert(scan.visited<3&&thread==scan.expected[scan.visited++]);
 return !scan.refuse;
}
void ScanThreads(native_cache::Service& service) {
 std::string error;size_t count=77;Scan scan{{&worker[0],&worker[1],&worker[2]}};
 Stop(nullptr,nullptr,false);assert(service.VisitStoppedWorld(ReadThread,&scan,&count,&error));Resume(nullptr);
 assert(count==3&&scan.visited==3&&!locked);
 scan.visited=0;scan.refuse=true;
 Stop(nullptr,nullptr,false);assert(!service.VisitStoppedWorld(ReadThread,&scan,&count,&error));Resume(nullptr);
 assert(count==3&&scan.visited==1&&!locked);
}
void Clear(native_cache::Service& service){
 std::string error;size_t count=77;
 Stop(nullptr,nullptr,false);assert(service.ClearStoppedWorld(&count,&error));Resume(nullptr);
 assert(count==3&&!stopped&&!locked);
}
}
int main(int argc,char** argv){
 if(argc==2)shape=std::stoi(argv[1]);assert(shape>=3&&shape<=5);
 Hooks hooks;native_cache::Service service;std::string error;size_t count=77;
 assert(!service.Bind(hooks,nullptr,&error));
 hooks.missing_checkpoint=true;assert(!service.Bind(hooks,&threads,&error)&&hooks.installs==0&&error=="Missing named native cache checkpoint capture entry");hooks.missing_checkpoint=false;
 assert(service.Bind(hooks,&threads,&error)&&hooks.installs==1);
 assert(!service.ClearStoppedWorld(&count,&error)&&count==77);
 wrong_owner=true;assert(!service.Prepare(&error));wrong_owner=false;
 conflict=true;assert(!service.Prepare(&error));conflict=false;
 throw_prepare=true;
 try{service.Prepare(&error);assert(false);}catch(const std::runtime_error&){}
 throw_prepare=false;
 assert(service.Prepare(&error));ScanThreads(service);Clear(service);assert(clears==3);
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
 assert(extension.Prepare(&error)&&extension.Prepared()&&extension.PreparedForCurrentThread());ScanThreads(extension);Clear(extension);
 foreign=true;assert(!extension.PreparedForCurrentThread());count=77;Stop(nullptr,nullptr,false);assert(!extension.ClearStoppedWorld(&count,&error)&&count==77);Resume(nullptr);foreign=false;
 Clear(extension);assert(clears==15&&vendor_calls>6&&!stopped&&!locked);
 std::cout<<"Native cache: original publication, owned capture, ambiguous/missing rejection, TLS recovery, actual vendor argument/owner capture, conflicts and wrong-thread rejection, synchronous enumeration passed\n";
}
