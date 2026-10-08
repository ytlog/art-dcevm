#include "platform/common/native_cache_service.h"
#include "platform/common/method_identity_calibration.h"
#include <cstring>
namespace artdcevm::native_cache {
namespace {
// Fixed android-15.0.0_r1 runtime/thread_pool.h: complete Closure interface.
// Calls target a real native static object initialized by its own named method.
class Closure {
 public:virtual ~Closure()=default;virtual void Run(void* self)=0;
};
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
struct Request {Closure* closure;size_t count=0;};
void ClearThread(void* thread,void* opaque) {
  auto& request=*static_cast<Request*>(opaque);
  request.closure->Run(thread);
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
  if(!candidate.closure_||!candidate.prepare_||!candidate.current_||!candidate.list_lock_||
     !candidate.lock_||!candidate.unlock_||!candidate.for_each_)
    return Fail(error,"Missing named native cache closure or synchronous enumeration services");
  candidate.threads_=observed_thread_list;
  *this=candidate;
  return true;
}
bool Service::Prepare(std::string* error) {
  if(!prepare_||!closure_||!current_||!current_())return Fail(error,"Unbound native cache service");
  prepare_(); // Establish real native static initialization, once per process.
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
