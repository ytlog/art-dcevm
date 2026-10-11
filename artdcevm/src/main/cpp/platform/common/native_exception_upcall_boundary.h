#pragma once
#include "platform/common/native_managed_stack_chain.h"
#include "platform/common/native_managed_stack_stream.h"
#include "platform/common/source_commit.h"
#include "support/named_symbol_extent.h"
namespace artdcevm::native_exception_upcall_boundary {
struct Report {
 size_t transitions=0,physical_frames=0,inline_frames=0,shadow_frames=0;
 uintptr_t terminal_pc=0;
 bool complete=false;const char*failure=nullptr;
};
class Service;
// Same-stop, same-Plan original upcall boundary only. Native roots, receivers,
// relocation and no-escape consumers remain independent commit obligations.
class Proof final {
 public:
 Proof(const Proof&)=delete;Proof&operator=(const Proof&)=delete;
 bool Validate(const source_commit::Plan&)const noexcept;
 private:
 friend class Service;
 Proof()=default;
 const Service*service_=nullptr;
 const native_managed_stack_chain::Chain*chain_=nullptr;
 const native_call_graph_stack::Service*stack_=nullptr;
 const native_call_graph_stack::Service::Prepared*stream_=nullptr;
 const native_source_stop::Session*stop_=nullptr;
 const modern_field_metadata::ReadContext*read_=nullptr;
 const source_commit::Plan*plan_=nullptr;void*self_=nullptr;
 Report report_{};
};
class Service final {
 public:
 Service()=default;Service(const Service&)=delete;Service&operator=(const Service&)=delete;
 // Named ELF extents only, prepared before stopping. No instruction decoding.
 bool Prepare(){
  if(prepared_)return true;
  prepared_=invoke_.Prepare("libart.so","art_quick_invoke_stub")&&
    invoke_static_.Prepare("libart.so","art_quick_invoke_static_stub");return prepared_;
 }
 bool Prepared()const noexcept{return prepared_;}
 // Source calibration observes scalars only; it never publishes a Plan proof.
 Report Observe(const native_managed_stack_chain::Chain&chain,
   const native_call_graph_stack::Service&stack,const native_call_graph_stack::Service::Prepared&stream,
   const native_source_stop::Session&stop,void*self,const modern_field_metadata::ReadContext&read)const noexcept{
  Report report;
  const auto deny=[&](const char*why){report.failure=why;return report;};
  if(!prepared_||chain.Thread()!=self||!chain.Fragments()||!chain.Validate()||
     !stack.ValidateStream(stream,stop,self,read))return deny("upcall-original-chain-or-stream-unavailable");
  const auto*frames=stack.Frames(stream);if(!frames)return deny("upcall-complete-stream-unavailable");
  native_managed_stack_stream::Cursor cursor;bool first=true;
  size_t physical=0,shadow=0;
  for(const auto&frame:*frames){
   if(frame.thread!=self)continue;
   if(first){
    const auto*head=chain.At(report.transitions);
    if(!head||!cursor.Begin(*head))return deny("upcall-fragment-head-unavailable");
    first=false;physical=shadow=0;
   }
   if(!cursor.Consume(frame,read))return deny("upcall-original-fragment-cursor-disconnected");
   if(frame.kind==native_call_graph_stack::FrameKind::Transition){
    if(!cursor.Complete())return deny("upcall-fragment-not-exhausted");
    // The oldest nonempty fragment must end in the named invoke boundary.
    // Empty tails retain that boundary; older shadow execution cannot borrow it.
    if(physical)report.terminal_pc=frame.pc;
    else if(shadow)report.terminal_pc=0;
    ++report.transitions;first=true;
   }else{
    if(frame.physical_boundary){++physical;++report.physical_frames;}
    if(frame.inline_depth)++report.inline_frames;
    if(frame.shadow){++shadow;++report.shadow_frames;}
   }
  }
  if(!first||report.transitions!=chain.Fragments()||!report.physical_frames||
     !chain.Validate()||!stack.ValidateStream(stream,stop,self,read))
   return deny("upcall-complete-chain-exhaustion-unproved");
  if(!report.terminal_pc||!(invoke_.ContainsReturnPc(report.terminal_pc)||
      invoke_static_.ContainsReturnPc(report.terminal_pc)))return deny("upcall-terminal-return-outside-named-invoke");
  report.complete=true;return report;
 }
 struct CaptureStorage final {
  CaptureStorage()=default;
  CaptureStorage(const CaptureStorage&)=delete;CaptureStorage&operator=(const CaptureStorage&)=delete;
  private:friend class Service;Proof proof_;
 };
 const Proof*Capture(CaptureStorage&storage,const source_commit::Plan&plan,
   const native_managed_stack_chain::Chain&chain,const native_call_graph_stack::Service&stack,
   const native_call_graph_stack::Service::Prepared&stream,const native_source_stop::Session&stop,
   void*self,const modern_field_metadata::ReadContext&read)const noexcept{
  auto&proof=storage.proof_;proof.service_=nullptr;
  const auto report=Observe(chain,stack,stream,stop,self,read);if(!report.complete)return nullptr;
  proof.chain_=&chain;proof.stack_=&stack;proof.stream_=&stream;proof.stop_=&stop;
  proof.read_=&read;proof.plan_=&plan;proof.self_=self;proof.report_=report;proof.service_=this;
  return &proof;
 }
 private:
 named_symbol_extent::Service invoke_,invoke_static_;bool prepared_=false;
};
inline bool Proof::Validate(const source_commit::Plan&plan)const noexcept{
 if(!service_||plan_!=&plan||!chain_||!stack_||!stream_||!stop_||!read_||!report_.complete)return false;
 const auto current=service_->Observe(*chain_,*stack_,*stream_,*stop_,self_,*read_);
 return current.complete&&current.terminal_pc==report_.terminal_pc&&current.transitions==report_.transitions&&
   current.physical_frames==report_.physical_frames&&current.inline_frames==report_.inline_frames&&
   current.shadow_frames==report_.shadow_frames;
}
}
