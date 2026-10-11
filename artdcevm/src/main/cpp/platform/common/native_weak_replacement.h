#pragma once
#include "platform/common/native_weak_sweep_observer.h"
#include "platform/common/native_weak_replacement_sequence.h"
#include "platform/common/exclusive_owned_roots.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/object_replacement_map.h"
#include <array>
namespace artdcevm::native_weak_replacement {
// Sole process-lifetime ingress owner. It observes the original replacement's
// own weak sweep, never starts another sweep or retains weak object addresses.
class Service final {
 public:
 static Service*Prepare(HookEngine&,JNIEnv*,jobject,const exclusive_owned_roots::Service&,
                       void*runtime,void*thread_list,native_weak_sweep_observer::CurrentThread,std::string*);
 using Replace=void(*)(void*,const void*);
 Report ReplaceReferences(const native_source_stop::Session&,void*self,
                         const object_replacement::ObjectReplacementMap&,Replace)const noexcept;
 bool BoundTo(void*runtime,void*thread_list)const noexcept{
  return installed_&&runtime_==runtime&&thread_list_==thread_list;
 }
 Service(const Service&)=delete;Service&operator=(const Service&)=delete;
 private:
 ~Service()=default;
 Service(HookEngine&hooks,void*runtime,void*thread_list,native_weak_sweep_observer::CurrentThread current)
  :hooks_(hooks),runtime_(runtime),thread_list_(thread_list),current_(current){}
 static void Sweep(void*,art::IsMarkedVisitor*);
 static void CacheSweep(const void*,art::IsMarkedVisitor*);
 static void OriginalCache(void*,art::IsMarkedVisitor*);
 static void Forward(size_t,void*,art::IsMarkedVisitor*);
 HookEngine&hooks_;void*runtime_;void*thread_list_;native_weak_sweep_observer::CurrentThread current_;
 std::array<void*,2>tokens_{};bool installed_=false;
};
}
