#pragma once
#include "platform/common/source_handle_roots.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/native_cache_service.h"
#include <vector>
namespace artdcevm::native_managed_stack_chain {
// This owns actual stopped registered-thread fragment facts only. It does not
// discharge an exception or authorize any native consumer on its own.
class Chain final {
 public:
 enum class Failure {None,Preparation,Owner,Head,Fragment,Cycle,Capacity,Changed};
 struct Result {Failure failure=Failure::Preparation;size_t fragments=0;bool complete=false;};
 Chain()=default;Chain(const Chain&)=delete;Chain&operator=(const Chain&)=delete;
 bool Prepare(size_t fragments){if(prepared_||!fragments)return false;records_.reserve(fragments);prepared_=true;return true;}
 Result Capture(const source_handle_roots::Service&source,const native_source_stop::Session&stop,
   void*self,void*thread,const native_cache::Service&threads,const modern_field_metadata::ReadContext&read)noexcept {
  source_handle_roots::ManagedStackSnapshot head;
  if(!source.ReadStoppedManagedStack(stop,self,thread,threads,read,&head))return Deny(Failure::Head);
  auto result=CaptureEnumerated(source,stop,self,thread,threads,read,head);
  if(result.complete&&!Validate())return Deny(Failure::Changed);return result;
 }
 private:

 Result CaptureEnumerated(const source_handle_roots::Service&source,const native_source_stop::Session&stop,
   void*self,void*thread,const native_cache::Service&threads,const modern_field_metadata::ReadContext&read,
   const source_handle_roots::ManagedStackSnapshot&head)noexcept {
  records_.clear();result_={};source_=&source;stop_=&stop;self_=self;thread_=thread;threads_=&threads;read_=&read;
  if(!prepared_||!stop.IsStopped(self))return Deny(Failure::Preparation);
  const auto census=managed_stack_source::Count(read,head.address);
  if(!census.complete)return Deny(Failure::Cycle);
  if(census.fragments>records_.capacity())return Deny(Failure::Capacity);
  auto*cursor=head.address;
  while(cursor){
   if(records_.size()==census.fragments)return Deny(Failure::Changed);
   source_handle_roots::ManagedStackSnapshot value;
   if(!source.ReadStoppedManagedFragment(stop,self,cursor,read,&value))return Deny(Failure::Fragment);
   records_.push_back(value);cursor=value.link;
  }
  if(records_.size()!=census.fragments||records_.empty()||!managed_stack_source::Same(records_.front(),head))return Deny(Failure::Changed);
  result_.failure=Failure::None;result_.fragments=records_.size();result_.complete=true;
  return result_;
 }
 public:
 bool Validate()const noexcept {
  if(!result_.complete||!source_||!stop_||!threads_||!read_||!stop_->IsStopped(self_)||records_.empty())return false;
  source_handle_roots::ManagedStackSnapshot current;
  if(!source_->ReadStoppedManagedStack(*stop_,self_,thread_,*threads_,*read_,&current)||!managed_stack_source::Same(current,records_.front()))return false;
  for(const auto&record:records_)if(!source_->ReadStoppedManagedFragment(*stop_,self_,record.address,*read_,&current)||!managed_stack_source::Same(current,record))return false;
  return stop_->IsStopped(self_);
 }
 size_t Fragments()const noexcept{return result_.complete?records_.size():0;}
 const source_handle_roots::ManagedStackSnapshot*At(size_t index)const noexcept {
  return result_.complete&&index<records_.size()?&records_[index]:nullptr;
 }
 void*Thread()const noexcept{return result_.complete?thread_:nullptr;}
 private:
 bool prepared_=false;std::vector<source_handle_roots::ManagedStackSnapshot> records_;Result result_{};
 const source_handle_roots::Service*source_=nullptr;const native_source_stop::Session*stop_=nullptr;
 const native_cache::Service*threads_=nullptr;const modern_field_metadata::ReadContext*read_=nullptr;
 void*self_=nullptr,*thread_=nullptr;
 Result Deny(Failure reason)noexcept{result_.failure=reason;result_.complete=false;result_.fragments=records_.size();return result_;}
};
}
