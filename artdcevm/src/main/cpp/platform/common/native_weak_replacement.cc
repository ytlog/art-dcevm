#include "platform/common/native_weak_replacement.h"
#include "platform/abi/common/jni_current_thread.h"
#include <atomic>
#include <cstdlib>
#include <mutex>
namespace artdcevm::native_weak_replacement {
namespace {
constexpr char kSweep[]="_ZN3art7Runtime16SweepSystemWeaksEPNS_15IsMarkedVisitorE";
std::mutex preparation;
std::atomic<Service*>owner{nullptr};void*original=nullptr;
struct Episode final:native_weak_sweep_observer::Sink {
 const Service&service;const native_source_stop::Session&stop;void*self;
 const object_replacement::ObjectReplacementMap&map;Report report{};
 Episode(const Service&s,const native_source_stop::Session&w,void*t,
         const object_replacement::ObjectReplacementMap&m):service(s),stop(w),self(t),map(m){}
 bool Observe(const native_weak_sweep_observer::Record&record)noexcept override{
  if(!stop.IsFinalWindow(self))return false;
  void*expected=map.Replacement(record.before);
  if(expected)++report.affected;else expected=record.before;
  return record.after==expected&&stop.IsFinalWindow(self);
 }
};
thread_local Episode*episode=nullptr;
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
}
Service*Service::Prepare(HookEngine&hooks,JNIEnv*env,jobject anchor,
 const exclusive_owned_roots::Service&exclusive,void*runtime,
 native_weak_sweep_observer::CurrentThread current,std::string*error){
 std::string ignored;if(!error)error=&ignored;
 std::unique_lock<std::mutex>lock(preparation,std::try_to_lock);
 if(!lock.owns_lock()||!env||env->ExceptionCheck()||!anchor||!runtime||!current||episode||
    reinterpret_cast<void*>(current)!=hooks.FindSymbol("libart.so",abi::common::kCurrentThread)){
  Fail(error,"weak-replacement-ordinary-owner-or-named-current-unavailable");return nullptr;
 }
 auto*service=owner.load(std::memory_order_acquire);
 if(service){
  if(&service->hooks_!=&hooks||service->runtime_!=runtime||service->current_!=current||!service->installed_){
   Fail(error,"weak-replacement-process-owner-differs-or-install-incomplete");return nullptr;
  }
  return service;
 }
 void*target=hooks.FindSymbol("libart.so",kSweep);
 if(!target){Fail(error,"weak-replacement-named-sweep-unavailable");return nullptr;}
 // Hooks can retain the owner even after a failed install/rollback; it is
 // deliberately process-lifetime, including every preparation failure.
 service=new Service(hooks,runtime,current);
 struct Request{Service*service;void*target;std::string*error;};Request request{service,target,error};
 const jobject roots[]{anchor};
 const bool installed=exclusive.With(env,roots,1,[](const exclusive_owned_roots::Context&actual,void*raw){
  auto&r=*static_cast<Request*>(raw);auto&s=*r.service;
  if(!actual.self||actual.self!=s.current_()||actual.count!=1||!actual.roots||!actual.roots[0])return false;
  owner.store(&s,std::memory_order_release);
  bool threw=false;
  try{s.token_=s.hooks_.Install(r.target,reinterpret_cast<void*>(Sweep),&original,r.error);}catch(...){threw=true;}
  if(!s.token_||!original){
   bool removed=!s.token_&&!threw;
   if(s.token_)try{removed=s.hooks_.Remove(s.token_,r.error);}catch(...){}
   if(removed){owner.store(nullptr,std::memory_order_release);original=nullptr;s.token_=nullptr;}
   return false;
  }
  s.installed_=true;return true;
 },&request,error);
 if(!installed||!service->installed_){
  if(owner.load(std::memory_order_acquire)!=service)delete service;
  Fail(error,"weak-replacement-exclusive-hook-install-incomplete");return nullptr;
 }
 return service;
}
void Service::Sweep(void*runtime,art::IsMarkedVisitor*visitor){
 const auto leaf=reinterpret_cast<native_weak_sweep_observer::OriginalSweep>(original);
 if(!leaf)std::abort();
 auto*service=owner.load(std::memory_order_acquire);auto*active=episode;
 if(!service||!active||&active->service!=service){leaf(runtime,visitor);return;}
 ++active->report.sweeps;
 native_weak_sweep_observer::Observer observer(service->runtime_,active->self,service->current_,leaf);
 observer.OnOriginalSweep(runtime,visitor,nullptr,0,active->report.sweep,active);
}
Report Service::ReplaceReferences(const native_source_stop::Session&stop,void*self,
 const object_replacement::ObjectReplacementMap&map,Replace replace)const noexcept{
 if(!installed_||owner.load(std::memory_order_acquire)!=this||!replace||episode||
    !self||current_()!=self||!stop.IsFinalWindow(self)||
    !object_replacement::VerifyObjectReplacementMap(map,nullptr))std::abort();
 Episode active(*this,stop,self,map);
 struct Scope{explicit Scope(Episode*value){episode=value;}~Scope(){episode=nullptr;}}scope(&active);
 replace(self,map.BorrowedMap());
 const auto&sweep=active.report.sweep;
 active.report.completed=active.report.sweeps==1&&sweep.status==native_weak_sweep_observer::Status::Completed&&
   !sweep.overflow&&!sweep.owner_changed&&!sweep.sink_rejected&&current_()==self&&stop.IsFinalWindow(self)&&
   object_replacement::VerifyObjectReplacementMap(map,nullptr);
 if(!active.report.completed)std::abort();return active.report;
}
}
