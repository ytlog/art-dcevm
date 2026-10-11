#pragma once
#include "platform/common/managed_stack_source.h"
#include "platform/common/native_call_graph_stack.h"
#include "platform/abi/arm64/android13/member_access_guards_abi.h"
namespace artdcevm::native_managed_stack_stream {
// Read-only correspondence for one original ManagedStack fragment. Callers
// separately own the actual stop, registered-thread chain and complete walker.
// This grants neither a native continuation nor exception/no-escape permission.
class Cursor final {
 public:
 bool Begin(const managed_stack_source::Snapshot&head)noexcept{
  *this=Cursor{};quick_=head.tagged_quick&~uintptr_t{3};shadow_=head.shadow;
  quick_fragment_=quick_!=0;shadow_fragment_=shadow_!=nullptr;
  if((head.tagged_quick&3u)==3u||(head.tagged_quick&&!quick_)||quick_%16||
     (quick_fragment_&&shadow_fragment_))return false;
  begun_=true;return true;
 }
 bool Consume(const native_call_graph_stack::Frame&frame,
              const modern_field_metadata::ReadContext&read)noexcept{
  if(!begun_||complete_||failed_)return false;
  const auto deny=[&](){failed_=true;return false;};
  using Kind=native_call_graph_stack::FrameKind;
  if(frame.kind==Kind::Transition){
   if(frame.method||frame.carrier_method||frame.shadow||frame.inline_depth||frame.physical_boundary||
      frame.return_slot||frame.return_pc||frame.return_carrier!=SIZE_MAX||pending_inline_)return deny();
   if(quick_fragment_){
    void*method=nullptr;
    if(uintptr_t(frame.quick)!=quick_||frame.pc!=pc_||
       !Load(read,frame.quick,0,&method)||method)return deny();
   }else if(frame.quick||frame.pc||shadow_||(shadow_fragment_&&!seen_))return deny();
   complete_=true;return true;
  }
  if(quick_fragment_){
   void*carrier=nullptr;
   if(frame.shadow||uintptr_t(frame.quick)!=quick_||frame.pc!=pc_||!frame.method||
      !Load(read,frame.quick,0,&carrier)||!carrier||frame.carrier_method!=carrier)return deny();
   if(pending_inline_&&frame.inline_depth!=pending_inline_-1)return deny();
   if(frame.inline_depth){
    if(frame.kind!=Kind::Managed||frame.physical_boundary||frame.return_slot||frame.return_pc||
       frame.return_carrier!=SIZE_MAX)return deny();
    pending_inline_=frame.inline_depth;
   }else{
    const size_t bytes=frame.geometry.size;
    if(!frame.physical_boundary||frame.method!=carrier||bytes<16||bytes%16||
       bytes>UINTPTR_MAX-quick_||!frame.return_slot||!frame.return_pc)return deny();
    const auto slot=uintptr_t(frame.return_slot);
    size_t extent=0;uintptr_t saved=0;
    if(slot%alignof(uintptr_t)||slot<quick_+sizeof(void*)||slot>quick_+bytes-sizeof(uintptr_t)||
       !read.owner||!read.extent||!read.extent(read.owner,frame.quick,&extent)||extent<bytes||
       !Load(read,frame.return_slot,0,&saved)||saved!=frame.return_pc)return deny();
    pending_inline_=0;quick_+=bytes;pc_=saved;
   }
  }else if(shadow_fragment_){
   using Shadow=android13::member_access_abi::ShadowFrame;
   void*link=nullptr,*method=nullptr;
   if(!shadow_||frame.shadow!=shadow_||frame.quick||frame.pc||frame.inline_depth||
      frame.physical_boundary||frame.return_slot||frame.return_pc||frame.return_carrier!=SIZE_MAX||
      !Load(read,shadow_,offsetof(Shadow,link),&link)||
      !Load(read,shadow_,offsetof(Shadow,method),&method)||!method||method!=frame.method)return deny();
   shadow_=link;
  }else return deny();
  seen_=true;return true;
 }
 bool Complete()const noexcept{return begun_&&complete_&&!failed_;}
 private:
 template<class T>static bool Load(const modern_field_metadata::ReadContext&read,
    const void*base,size_t offset,T*out)noexcept{
  size_t extent=0;
  if(!base||!out||!read.owner||!read.extent||offset>SIZE_MAX-sizeof(T)||
     offset+sizeof(T)>UINTPTR_MAX-uintptr_t(base)||
     !read.extent(read.owner,base,&extent)||extent<offset+sizeof(T))return false;
  std::memcpy(out,reinterpret_cast<const void*>(uintptr_t(base)+offset),sizeof(T));return true;
 }
 uintptr_t quick_=0,pc_=0;void*shadow_=nullptr;size_t pending_inline_=0;
 bool begun_=false,quick_fragment_=false,shadow_fragment_=false,seen_=false,complete_=false,failed_=false;
};
}
