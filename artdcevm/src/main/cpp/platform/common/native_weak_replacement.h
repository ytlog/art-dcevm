#pragma once
#include "platform/common/native_weak_sweep_observer.h"
#include "platform/common/exclusive_owned_roots.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/object_replacement_map.h"
namespace artdcevm::native_weak_replacement {
struct Report {
 native_weak_sweep_observer::Report sweep{};
 size_t sweeps=0,affected=0;
 bool completed=false;
};
// Sole process-lifetime ingress owner. It observes the original replacement's
// own weak sweep, never starts another sweep or retains weak object addresses.
class Service final {
 public:
 static Service*Prepare(HookEngine&,JNIEnv*,jobject,const exclusive_owned_roots::Service&,
                       void*runtime,native_weak_sweep_observer::CurrentThread,std::string*);
 using Replace=void(*)(void*,const void*);
 Report ReplaceReferences(const native_source_stop::Session&,void*self,
                         const object_replacement::ObjectReplacementMap&,Replace)const noexcept;
 bool BoundTo(void*runtime)const noexcept{return installed_&&runtime_==runtime;}
 Service(const Service&)=delete;Service&operator=(const Service&)=delete;
 private:
 ~Service()=default;
 Service(HookEngine&hooks,void*runtime,native_weak_sweep_observer::CurrentThread current)
  :hooks_(hooks),runtime_(runtime),current_(current){}
 static void Sweep(void*,art::IsMarkedVisitor*);
 HookEngine&hooks_;void*runtime_;native_weak_sweep_observer::CurrentThread current_;
 void*token_=nullptr;bool installed_=false;
};
}
