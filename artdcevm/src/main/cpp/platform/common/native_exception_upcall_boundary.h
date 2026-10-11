#pragma once
#include "platform/common/native_managed_stack_chain.h"
#include "platform/common/source_commit.h"
#include "support/named_symbol_extent.h"
namespace artdcevm::native_exception_upcall_boundary {
namespace detail {
// Source FrameInfo's return-PC offset, never inferred from machine instructions.
inline bool SavedReturn(const modern_field_metadata::ReadContext&read,uintptr_t quick,
 size_t bytes,size_t return_offset,uintptr_t*out)noexcept {
 if(!out||!quick||quick%16||bytes<16||bytes%16||bytes>UINTPTR_MAX-quick||
    return_offset!=bytes-sizeof(uintptr_t)||!read.owner||!read.extent)return false;
 size_t extent=0;if(!read.extent(read.owner,reinterpret_cast<const void*>(quick),&extent)||extent<bytes)return false;
 uintptr_t pc=0;std::memcpy(&pc,reinterpret_cast<const void*>(quick+return_offset),sizeof(pc));
 if(!pc)return false;*out=pc;return true;
}
}
// This capability proves only the original managed-to-native upcall boundary.
// Root relocation, JNI ownership, native consumers and retirement remain separate
// same-Plan obligations. Only the concrete original-walker Service can produce it.
class Proof final {
 public:
 enum class Failure:uint32_t {None,Unprepared,Geometry,Transition,Incomplete,SymbolExtent};
 Failure FailureCode()const noexcept{return failure_;}
 size_t Transitions()const noexcept{return fragment_;}
 Proof(const Proof&)=delete;Proof&operator=(const Proof&)=delete;
 bool Validate(const source_commit::Plan&plan)const noexcept {
  return sealed_&&!failed_&&plan_==&plan&&chain_&&chain_->Validate()&&last_lr_==last_transition_&&
    (invoke_.ContainsReturnPc(last_lr_)||invoke_static_.ContainsReturnPc(last_lr_));
 }
 private:

 Proof()=default;
 bool Prepare(){prepared_=invoke_.Prepare("libart.so","art_quick_invoke_stub")&&
  invoke_static_.Prepare("libart.so","art_quick_invoke_static_stub");return prepared_;}
 bool Begin(const native_managed_stack_chain::Chain&chain,const source_commit::Plan&plan)noexcept {
  sealed_=false;chain_=&chain;plan_=&plan;fragment_=0;last_lr_=0;last_transition_=0;next_quick_=0;physical_=false;managed_terminal_=false;failed_=false;failure_=Failure::None;
  if(!prepared_||!chain.Fragments())return Refuse(Failure::Unprepared);return true;
 }
 // Call for every physical frame, after the original named FrameInfo reader and
 // same-cursor method/quick provenance are already proved by the walker.
 bool Physical(uintptr_t quick,uintptr_t pc,size_t frame_bytes,size_t return_offset,
 const modern_field_metadata::ReadContext&read)noexcept {
  if(failed_||sealed_||!chain_||fragment_>=chain_->Fragments())return Refuse();
  const auto*fragment=chain_->At(fragment_);
  if(!fragment||fragment->shadow||
    (!physical_&&(quick!=(fragment->tagged_quick&~uintptr_t{3})||pc!=0))||
    (physical_&&(quick!=next_quick_||pc!=last_lr_)))return Refuse();
  uintptr_t lr=0;if(!detail::SavedReturn(read,quick,frame_bytes,return_offset,&lr))return Refuse();
  next_quick_=quick+frame_bytes;last_lr_=lr;physical_=true;return true;
 }
 // The original walker emits a transition for every fragment, including empty
 // ones. Match its actual cursor, not an expected fragment pointer supplied by
 // the caller. No runtime/native/shadow callback may be discarded.
 bool Transition(uintptr_t actual_quick,uintptr_t actual_pc,
 const modern_field_metadata::ReadContext&read)noexcept {
  if(failed_||sealed_||!chain_)return Refuse(Failure::Transition);
  const auto*source=chain_->At(fragment_);
  if(!source||source->shadow)return Refuse(Failure::Transition);
  if(physical_){
   if(last_lr_!=actual_pc||next_quick_!=actual_quick||!read.owner||!read.extent)return Refuse(Failure::Transition);
   size_t extent=0;uintptr_t method=1;
   if(!read.extent(read.owner,reinterpret_cast<const void*>(actual_quick),&extent)||extent<sizeof(method))return Refuse(Failure::Transition);
   std::memcpy(&method,reinterpret_cast<const void*>(actual_quick),sizeof(method));if(method)return Refuse(Failure::Transition);
   last_transition_=actual_pc;
  }else if(source->tagged_quick||actual_quick||actual_pc)return Refuse(Failure::Transition);
  // Verified empty tails reset the original cursor to null/zero. They add
  // no older managed handler, but grant nothing about native caller behavior.
  if(physical_)managed_terminal_=true;++fragment_;physical_=false;next_quick_=0;return true;
 }
 bool ExhaustedNormally()noexcept {
  if(failed_)return false;
  if(physical_||!managed_terminal_||!chain_||fragment_!=chain_->Fragments()||!last_lr_||
    !last_transition_||last_lr_!=last_transition_)return Refuse(Failure::Incomplete);
  const auto*terminal=chain_->At(fragment_-1);if(!terminal||terminal->link)return Refuse(Failure::Incomplete);
  if(!(invoke_.ContainsReturnPc(last_lr_)||invoke_static_.ContainsReturnPc(last_lr_)))return Refuse(Failure::SymbolExtent);
  sealed_=true;return true;
 }
 bool Refuse(Failure reason=Failure::Geometry)noexcept {if(!failed_)failure_=reason;failed_=true;sealed_=false;return false;}
 named_symbol_extent::Service invoke_,invoke_static_;
 const native_managed_stack_chain::Chain*chain_=nullptr;const source_commit::Plan*plan_=nullptr;
 size_t fragment_=0;uintptr_t last_lr_=0,last_transition_=0,next_quick_=0;
 Failure failure_=Failure::Unprepared;
 bool prepared_=false,sealed_=false,physical_=false,managed_terminal_=false,failed_=false;
};
}
