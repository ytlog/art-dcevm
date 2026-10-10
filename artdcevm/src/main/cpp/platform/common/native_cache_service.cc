#include "platform/common/native_cache_service.h"
#include "platform/abi/arm64/common/closure.h"
#include "platform/common/method_identity_calibration.h"
#include <cstring>
#include <mutex>
#include <atomic>
#include "platform/abi/arm64/common/checkpoint.h"
#include "platform/abi/common/suspend_all.h"
namespace artdcevm::native_cache {
namespace {
using Closure=art::Closure;
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
// Observe the native owner's function-static Closure only during its synchronous
// ClearAll window, with the independently observed ThreadList receiver. All
// original arguments/results pass through the exact fixed-source signature.
using namespace abi::arm64::android_common; using namespace ::artdcevm::abi::common;
struct Capture {void* threads;void* closure=nullptr;bool conflict=false;};
thread_local Capture* capturing=nullptr;
void* original_checkpoint_storage=nullptr;
std::mutex checkpoint_installation;
void* checkpoint_token=nullptr;
void ObserveOwnedClosure(void* threads,Closure* closure,Closure* callback,bool checking,bool acquire){
 if(capturing&&threads==capturing->threads&&closure&&!callback&&checking&&!acquire){
  if(capturing->closure&&capturing->closure!=closure)capturing->conflict=true;
  else capturing->closure=closure;
 }
}
size_t ObserveCheckpointMutator(void* threads,Closure* closure,Closure* callback,bool checking,bool acquire){
 ObserveOwnedClosure(threads,closure,callback,checking,acquire);
 auto original=reinterpret_cast<CheckpointMutator>(original_checkpoint_storage);
 if(!original)std::abort();return original(threads,closure,callback,checking,acquire);
}
size_t ObserveCheckpointChecking(void* threads,Closure* closure,Closure* callback,bool checking){
 ObserveOwnedClosure(threads,closure,callback,checking,false);
 auto original=reinterpret_cast<CheckpointChecking>(original_checkpoint_storage);
 if(!original)std::abort();return original(threads,closure,callback,checking);
}
size_t ObserveCheckpointCallback(void* threads,Closure* closure,Closure* callback){
 ObserveOwnedClosure(threads,closure,callback,true,false);
 auto original=reinterpret_cast<CheckpointCallback>(original_checkpoint_storage);
 if(!original)std::abort();return original(threads,closure,callback);
}
bool InstallCheckpoint(HookEngine& hooks,std::string* error){
 std::lock_guard<std::mutex> guard(checkpoint_installation);
 if(checkpoint_token)return true;
 void* target=nullptr;void* proxy=nullptr;
 struct Entry{const char* name;void* proxy;};
 const Entry entries[]{
  {kCheckpointMutator,reinterpret_cast<void*>(ObserveCheckpointMutator)},
  {kCheckpointChecking,reinterpret_cast<void*>(ObserveCheckpointChecking)},
  {kCheckpointCallback,reinterpret_cast<void*>(ObserveCheckpointCallback)}};
 for(const auto& entry:entries){target=hooks.FindSymbol("libart.so",entry.name);if(target){proxy=entry.proxy;break;}}
 auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!target||!construct||!destroy)return Fail(error,"Missing named native cache checkpoint capture entry");
 SuspendAll stopped{};construct(&stopped,"artdcevm native cache closure capture",false);
 checkpoint_token=hooks.Install(target,proxy,&original_checkpoint_storage,error);
 if(checkpoint_token&&!original_checkpoint_storage)std::abort();
 destroy(&stopped);
 return checkpoint_token&&original_checkpoint_storage;
}
// Some vendor ELFs expose an additional unsigned argument absent from the
// fixed AOSP Mutex declaration. Its meaning/default is deliberately unknown.
// Capture only the actual native owner's value for the observed ThreadList
// mutex and current Thread, and pass it through unchanged. Never infer a value.
using VendorLock=void (*)(void*,void*,uint32_t);
struct LockCapture {void* mutex;void* self;uint32_t argument=0;bool observed=false,conflict=false;};
thread_local LockCapture* capturing_lock=nullptr;
std::mutex vendor_installation;
void* original_vendor_storage=nullptr;void* vendor_token=nullptr;
std::atomic<VendorMutexReturned> vendor_returned{nullptr};
void ObserveVendorLock(void* mutex,void* self,uint32_t argument){
  if(capturing_lock&&mutex==capturing_lock->mutex&&self==capturing_lock->self){
    if(capturing_lock->observed&&capturing_lock->argument!=argument)capturing_lock->conflict=true;
    capturing_lock->argument=argument;capturing_lock->observed=true;
  }
  auto original=reinterpret_cast<VendorLock>(original_vendor_storage);
  if(!original)std::abort();original(mutex,self,argument);
  if(auto returned=vendor_returned.load(std::memory_order_acquire))returned(mutex,self,argument);
}
bool InstallVendorLock(HookEngine& hooks,std::string* error){
  std::lock_guard<std::mutex> guard(vendor_installation);if(vendor_token)return true;
  using namespace abi::arm64::android_common; using namespace ::artdcevm::abi::common;
  auto target=hooks.FindSymbol("libart.so","_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadEj");
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!target||!construct||!destroy)return Fail(error,"Missing exact vendor mutex observation entry");
  SuspendAll stop{};construct(&stop,"artdcevm native cache mutex observation",false);
  vendor_token=hooks.Install(target,reinterpret_cast<void*>(ObserveVendorLock),&original_vendor_storage,error);
  if(vendor_token&&!original_vendor_storage)std::abort();destroy(&stop);
  return vendor_token&&original_vendor_storage;
}
struct Request {Closure* closure;size_t count=0;};
void ClearThread(void* thread,void* opaque) {
  auto& request=*static_cast<Request*>(opaque);
  request.closure->Run(static_cast<art::Thread*>(thread));
  ++request.count;
}
}
bool ObserveVendorMutexReturns(HookEngine& hooks,VendorMutexReturned returned,std::string* error){
 if(!returned)return Fail(error,"OEM mutex return observer requires a typed consumer");
 if(!InstallVendorLock(hooks,error))return false;
 auto expected=static_cast<VendorMutexReturned>(nullptr);
 if(!vendor_returned.compare_exchange_strong(expected,returned,std::memory_order_acq_rel,std::memory_order_acquire)&&expected!=returned)
  return Fail(error,"OEM mutex normal-return consumer already has a different owner");
 return true;
}
bool Service::Bind(HookEngine& hooks,void* observed_thread_list,std::string* error) {
  if(!observed_thread_list)return Fail(error,"Native cache service requires observed ThreadList");
  Service candidate;
#define ART(member,symbol) candidate.member=reinterpret_cast<decltype(candidate.member)>(hooks.FindSymbol("libart.so",symbol))
  ART(closure_,"_ZZN3art6Thread25ClearAllInterpreterCachesEvE7closure");
  ART(prepare_,"_ZN3art6Thread25ClearAllInterpreterCachesEv");
  ART(current_,"_ZN3art6Thread14CurrentFromGdbEv");
  ART(list_lock_,"_ZN3art5Locks17thread_list_lock_E");
  ART(lock_,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE");
  if(!candidate.lock_)ART(vendor_lock_,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadEj");
  ART(unlock_,"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE");
  ART(for_each_,"_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_");
#undef ART
  if(!candidate.prepare_||!candidate.current_||!candidate.list_lock_||
     (!candidate.lock_&&!candidate.vendor_lock_)||!candidate.unlock_||!candidate.for_each_)
    return Fail(error,"Missing named native cache closure or synchronous enumeration services");
  if(candidate.vendor_lock_&&!InstallVendorLock(hooks,error))return false;
  if(!candidate.closure_&&!InstallCheckpoint(hooks,error))return false;
  candidate.threads_=observed_thread_list;
  *this=candidate;
  return true;
}
bool Service::Prepare(std::string* error) {
  if(!prepare_||!threads_||!current_||!current_()||capturing)return Fail(error,"Unbound native cache service");
  if(vendor_lock_&&(!list_lock_||!*list_lock_||capturing_lock))return Fail(error,"Missing vendor mutex capture context");
  LockCapture lock_capture{list_lock_?*list_lock_:nullptr,current_()};
  Capture capture{threads_};
  {
    struct Window {
      Capture* previous;
      explicit Window(Capture* value):previous(capturing){capturing=value;}
      ~Window(){capturing=previous;}
    } window(&capture);
    struct LockWindow {
      LockCapture* previous;
      explicit LockWindow(LockCapture* value):previous(capturing_lock){capturing_lock=value;}
      ~LockWindow(){capturing_lock=previous;}
    } lock_window(vendor_lock_?&lock_capture:nullptr);
    prepare_(); // The real owner initializes and passes its static Closure.
  }
  if(!closure_) {
    if(capture.conflict||!capture.closure)return Fail(error,"Native cache owner did not supply a unique observed Closure");
    closure_=capture.closure;
  } else if(capture.conflict||(capture.closure&&capture.closure!=closure_))
    return Fail(error,"Native cache observed Closure disagrees with named static object");
  if(vendor_lock_){
    if(lock_capture.conflict||!lock_capture.observed)return Fail(error,"Native cache owner did not supply a unique vendor mutex argument");
    vendor_lock_argument_=lock_capture.argument;vendor_lock_self_=lock_capture.self;vendor_lock_observed_=true;
  }
  size_t extent=0;
  if(!method_identity::ReadableMappingExtent(closure_,&extent)||extent<sizeof(void*))
    return Fail(error,"Native cache closure object is not readable");
  void* vptr=nullptr;std::memcpy(&vptr,closure_,sizeof(vptr));
  if(!vptr||!method_identity::ReadableMappingExtent(vptr,&extent)||extent<3*sizeof(void*))
    return Fail(error,"Native cache closure was not initialized by its named owner");
  prepared_=true;
  return true;
}
bool Service::PreparedForCurrentThread()const {
  void* self=current_?current_():nullptr;
  return prepared_&&self&&(!vendor_lock_||(vendor_lock_observed_&&vendor_lock_self_==self));
}
bool Service::VisitStoppedWorld(StoppedVisitor visitor,void* state,size_t* registered_threads,std::string* error)const {
  if(!prepared_||!visitor||!registered_threads||!list_lock_||!*list_lock_)
    return Fail(error,"Native stopped enumeration is not prepared");
  void* self=current_();if(!self)return Fail(error,"Native cache current Thread unavailable");
  if(vendor_lock_){if(!vendor_lock_observed_||self!=vendor_lock_self_)return Fail(error,"Vendor mutex argument is not calibrated for this native owner Thread");vendor_lock_(*list_lock_,self,vendor_lock_argument_);}
  else lock_(*list_lock_,self);
  struct UnlockScope {
    void (*unlock)(void*,void*);void* mutex;void* self;
    ~UnlockScope(){unlock(mutex,self);}
  } unlock_scope{unlock_,*list_lock_,self};
  struct Enumeration {StoppedVisitor visitor;void* state;size_t count=0;bool accepted=true;};
  Enumeration enumeration{visitor,state};
  for_each_(threads_,[](void* thread,void* raw){
    auto& e=*static_cast<Enumeration*>(raw);++e.count;
    if(e.accepted)e.accepted=thread&&e.visitor(thread,e.state);
  },&enumeration);
  *registered_threads=enumeration.count;
  if(!enumeration.accepted)return Fail(error,"Native stopped Thread visitor refused");
  return enumeration.count!=0||Fail(error,"Native stopped enumeration had no Threads");
}
bool Service::ClearStoppedWorld(size_t* cleared_threads,std::string* error)const {
  if(!cleared_threads)return Fail(error,"Native cache closure is not prepared");
  Request request{static_cast<Closure*>(closure_)};
  size_t registered=0;
  if(!VisitStoppedWorld([](void* thread,void* state){ClearThread(thread,state);return true;},&request,&registered,error))return false;
  *cleared_threads=request.count;
  return true;
}
}
