#include "platform/common/native_weak_replacement.h"
#include "platform/abi/common/jni_current_thread.h"
#include <atomic>
#include <cstdlib>
#include <mutex>
namespace artdcevm::native_weak_replacement {
namespace {
constexpr const char* kSweeps[]{
 "_ZN3art7Runtime16SweepSystemWeaksEPNS_15IsMarkedVisitorE",
 "_ZNK3art10ThreadList22SweepInterpreterCachesEPNS_15IsMarkedVisitorE"};
std::mutex preparation;
std::atomic<Service*>owner{nullptr};std::array<void*,2>originals{};
struct Episode final:native_weak_sweep_observer::Sink {
 const Service&service;const native_source_stop::Session&stop;void*self;
 const object_replacement::ObjectReplacementMap&map;Report report{};Sequence sequence{report};
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
 const exclusive_owned_roots::Service&exclusive,void*runtime,void*thread_list,
 native_weak_sweep_observer::CurrentThread current,std::string*error){
 std::string ignored;if(!error)error=&ignored;
 std::unique_lock<std::mutex>lock(preparation,std::try_to_lock);
 if(!lock.owns_lock()||!env||env->ExceptionCheck()||!anchor||!runtime||!thread_list||!current||episode||
    reinterpret_cast<void*>(current)!=hooks.FindSymbol("libart.so",abi::common::kCurrentThread)){
  Fail(error,"weak-replacement-ordinary-owner-or-named-current-unavailable");return nullptr;
 }
 auto*service=owner.load(std::memory_order_acquire);
 if(service){
  if(&service->hooks_!=&hooks||service->runtime_!=runtime||service->thread_list_!=thread_list||service->current_!=current||!service->installed_){
   Fail(error,"weak-replacement-process-owner-differs-or-install-incomplete");return nullptr;
  }
  return service;
 }
 std::array<void*,2>targets{};
 for(size_t i=0;i<targets.size();++i){
  targets[i]=hooks.FindSymbol("libart.so",kSweeps[i]);
  if(!targets[i]){Fail(error,"weak-replacement-named-sweep-unavailable");return nullptr;}
 }
 // Hooks can retain the owner even after a failed install/rollback; it is
 // process-lifetime whenever a callback might remain; clean failures dispose it.
 service=new Service(hooks,runtime,thread_list,current);
 struct Request{Service*service;std::array<void*,2>targets;std::string*error;};Request request{service,targets,error};
 const jobject roots[]{anchor};
 const bool installed=exclusive.With(env,roots,1,[](const exclusive_owned_roots::Context&actual,void*raw){
  auto&r=*static_cast<Request*>(raw);auto&s=*r.service;
  if(!actual.self||actual.self!=s.current_()||actual.count!=1||!actual.roots||!actual.roots[0])return false;
  owner.store(&s,std::memory_order_release);
  const std::array<void*,2>proxies{reinterpret_cast<void*>(Sweep),reinterpret_cast<void*>(CacheSweep)};
  bool threw=false;size_t installed=0;
  try{
   for(size_t i=0;i<proxies.size();++i){
    s.tokens_[i]=s.hooks_.Install(r.targets[i],proxies[i],&originals[i],r.error);
    if(!s.tokens_[i]||!originals[i])break;
    ++installed;
   }
  }catch(...){threw=true;}
  if(installed!=proxies.size()){
   bool removed=!threw;
   for(size_t i=s.tokens_.size();i;--i)if(s.tokens_[i-1]){
    try{if(!s.hooks_.Remove(s.tokens_[i-1],r.error))removed=false;}catch(...){removed=false;}
   }
   if(removed){owner.store(nullptr,std::memory_order_release);originals={};s.tokens_={};}
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
void Service::Sweep(void*runtime,art::IsMarkedVisitor*visitor){Forward(0,runtime,visitor);}
void Service::CacheSweep(const void*thread_list,art::IsMarkedVisitor*visitor){
 Forward(1,const_cast<void*>(thread_list),visitor);
}
void Service::OriginalCache(void*thread_list,art::IsMarkedVisitor*visitor){
 // Fixed ThreadList declaration is const; only this ABI adapter differs.
 const auto leaf=reinterpret_cast<void(*)(const void*,art::IsMarkedVisitor*)>(originals[1]);
 if(!leaf)std::abort();leaf(thread_list,visitor);
}
void Service::Forward(size_t index,void*actual,art::IsMarkedVisitor*visitor){
 if(index>=originals.size()||!originals[index])std::abort();
 const auto leaf=index?OriginalCache:reinterpret_cast<native_weak_sweep_observer::OriginalSweep>(originals[0]);
 auto*service=owner.load(std::memory_order_acquire);auto*active=episode;
 if(!service||!active||&active->service!=service){leaf(actual,visitor);return;}
 auto&report=active->report;
 active->sequence.Advance(index,visitor);
 native_weak_sweep_observer::Observer observer(index?service->thread_list_:service->runtime_,
   active->self,service->current_,leaf);
 observer.OnOriginalSweep(actual,visitor,nullptr,0,index?report.cache_sweep:report.sweep,active);
}
Report Service::ReplaceReferences(const native_source_stop::Session&stop,void*self,
 const object_replacement::ObjectReplacementMap&map,Replace replace)const noexcept{
 if(!installed_||owner.load(std::memory_order_acquire)!=this||!replace||episode||
    !self||current_()!=self||!stop.IsFinalWindow(self)||
    !object_replacement::VerifyObjectReplacementMap(map,nullptr))std::abort();
 Episode active(*this,stop,self,map);
 struct Scope{explicit Scope(Episode*value){episode=value;}~Scope(){episode=nullptr;}}scope(&active);
 replace(self,map.BorrowedMap());
 const auto complete=[](const native_weak_sweep_observer::Report&report){
  return report.status==native_weak_sweep_observer::Status::Completed&&
    !report.overflow&&!report.owner_changed&&!report.sink_rejected;
 };
 active.report.completed=active.sequence.Complete()&&
   complete(active.report.sweep)&&complete(active.report.cache_sweep)&&
   current_()==self&&stop.IsFinalWindow(self)&&object_replacement::VerifyObjectReplacementMap(map,nullptr);
 if(!active.report.completed)std::abort();return active.report;
}
}
