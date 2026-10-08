#include "platform/common/native_cache_service.h"
#include "platform/abi/arm64/android-common/closure.h"
#include "platform/common/method_identity_calibration.h"
#include <cstring>
#include <mutex>
#include "platform/abi/arm64/android-common/suspend_all.h"
namespace artdcevm::native_cache {
namespace {
using Closure=art::Closure;
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
// Fixed Android17 thread_list.h: the real native owner passes its function-
// static Closure to this named entry. Observe only our synchronous ClearAll
// window, with the independently observed ThreadList receiver.
using Checkpoint=size_t (*)(void*,void*,void*,bool,bool);
struct Capture {void* threads;void* closure=nullptr;bool conflict=false;};
thread_local Capture* capturing=nullptr;
void* original_checkpoint_storage=nullptr;
std::mutex checkpoint_installation;
void* checkpoint_token=nullptr;
size_t ObserveCheckpoint(void* threads,void* closure,void* callback,bool checking,bool acquire) {
 if(capturing&&threads==capturing->threads&&closure&&!callback&&checking&&!acquire) {
  if(capturing->closure&&capturing->closure!=closure)capturing->conflict=true;
  else capturing->closure=closure;
 }
 auto original=reinterpret_cast<Checkpoint>(original_checkpoint_storage);
 if(!original)std::abort();
 return original(threads,closure,callback,checking,acquire);
}
bool InstallCheckpoint(HookEngine& hooks,std::string* error) {
 std::lock_guard<std::mutex> guard(checkpoint_installation);
 if(checkpoint_token)return true;
 auto target=hooks.FindSymbol("libart.so","_ZN3art10ThreadList13RunCheckpointEPNS_7ClosureES2_bb");
 using namespace abi::arm64::android_common;
 auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!target||!construct||!destroy)return Fail(error,"Missing named native cache checkpoint capture entry");
 SuspendAll stopped{};construct(&stopped,"artdcevm native cache closure capture",false);
 checkpoint_token=hooks.Install(target,reinterpret_cast<void*>(ObserveCheckpoint),&original_checkpoint_storage,error);
 if(checkpoint_token&&!original_checkpoint_storage)std::abort();
 destroy(&stopped);
 return checkpoint_token&&original_checkpoint_storage;
}
struct Request {Closure* closure;size_t count=0;};
void ClearThread(void* thread,void* opaque) {
  auto& request=*static_cast<Request*>(opaque);
  request.closure->Run(static_cast<art::Thread*>(thread));
  ++request.count;
}
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
  ART(unlock_,"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE");
  ART(for_each_,"_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_");
#undef ART
  if(!candidate.prepare_||!candidate.current_||!candidate.list_lock_||
     !candidate.lock_||!candidate.unlock_||!candidate.for_each_)
    return Fail(error,"Missing named native cache closure or synchronous enumeration services");
  if(!candidate.closure_&&!InstallCheckpoint(hooks,error))return false;
  candidate.threads_=observed_thread_list;
  *this=candidate;
  return true;
}
bool Service::Prepare(std::string* error) {
  if(!prepare_||!threads_||!current_||!current_()||capturing)return Fail(error,"Unbound native cache service");
  Capture capture{threads_};
  {
    struct Window {
      Capture* previous;
      explicit Window(Capture* value):previous(capturing){capturing=value;}
      ~Window(){capturing=previous;}
    } window(&capture);
    prepare_(); // The real owner initializes and passes its static Closure.
  }
  if(!closure_) {
    if(capture.conflict||!capture.closure)return Fail(error,"Native cache owner did not supply a unique observed Closure");
    closure_=capture.closure;
  } else if(capture.conflict||(capture.closure&&capture.closure!=closure_))
    return Fail(error,"Native cache observed Closure disagrees with named static object");
  size_t extent=0;
  if(!method_identity::ReadableMappingExtent(closure_,&extent)||extent<sizeof(void*))
    return Fail(error,"Native cache closure object is not readable");
  void* vptr=nullptr;std::memcpy(&vptr,closure_,sizeof(vptr));
  if(!vptr||!method_identity::ReadableMappingExtent(vptr,&extent)||extent<3*sizeof(void*))
    return Fail(error,"Native cache closure was not initialized by its named owner");
  prepared_=true;
  return true;
}
bool Service::ClearStoppedWorld(size_t* cleared_threads,std::string* error)const {
  if(!prepared_||!cleared_threads||!list_lock_||!*list_lock_)
    return Fail(error,"Native cache closure is not prepared");
  void* self=current_();if(!self)return Fail(error,"Native cache current Thread unavailable");
  Request request{static_cast<Closure*>(closure_)};
  lock_(*list_lock_,self);
  for_each_(threads_,ClearThread,&request);
  unlock_(*list_lock_,self);
  *cleared_threads=request.count;
  return request.count!=0||Fail(error,"Native cache enumeration had no Threads");
}
}
