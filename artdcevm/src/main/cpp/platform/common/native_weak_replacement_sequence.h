#pragma once
#include "platform/common/native_weak_sweep_observer.h"
#include <cstdint>
namespace artdcevm::native_weak_replacement {
struct Report {
 native_weak_sweep_observer::Report sweep{},cache_sweep{};
 size_t sweeps=0,cache_sweeps=0,affected=0;
 bool order_changed=false,visitor_changed=false,completed=false;
};
// Synchronous ingress facts only. The visitor identity dies with this episode;
// neither ordering nor an identical visitor supplies source/commit permission.
class Sequence final {
 public:
 explicit Sequence(Report&report):report_(report){}
 Sequence(const Sequence&)=delete;Sequence&operator=(const Sequence&)=delete;
 void Advance(size_t index,art::IsMarkedVisitor*visitor)noexcept{
  if(index==0){
   if(report_.sweeps||report_.cache_sweeps)report_.order_changed=true;
   else visitor_=visitor;
   if(report_.sweeps==SIZE_MAX)report_.order_changed=true;else ++report_.sweeps;
  }else if(index==1){
   if(report_.sweeps!=1||report_.cache_sweeps)report_.order_changed=true;
   if(visitor!=visitor_)report_.visitor_changed=true;
   if(report_.cache_sweeps==SIZE_MAX)report_.order_changed=true;else ++report_.cache_sweeps;
  }else report_.order_changed=true;
  if(!visitor)report_.visitor_changed=true;
 }
 bool Complete()const noexcept{
  return report_.sweeps==1&&report_.cache_sweeps==1&&!report_.order_changed&&!report_.visitor_changed;
 }
 private:
 Report&report_;art::IsMarkedVisitor*visitor_=nullptr;
};
}
