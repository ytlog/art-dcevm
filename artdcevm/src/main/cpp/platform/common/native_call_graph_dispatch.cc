#include "platform/common/native_call_graph_dispatch.h"
#include "platform/common/native_interface_graph.h"
#include "platform/common/native_interface_dispatch.h"
#include "platform/abi/arm64/common/interface_table.h"
namespace artdcevm::native_call_graph_dispatch {
bool Reader::Observe(native_call_graph::InvokeKind kind,void*declaration,void*receiver,
 const modern_field_metadata::ReadContext&read,Observation*out,const char**failure)const noexcept{
 if(out)*out={};if(failure)*failure=nullptr;
 auto deny=[&](const char*s){if(failure)*failure=s;return false;};
 auto extent=[&](const void*p,size_t n){size_t available=0;return p&&read.extent(read.owner,p,&available)&&available>=n;};
 if(!out||!read.owner||!read.extent||!metadata_.Calibrated()||!declaration||!receiver||
 (kind!=native_call_graph::InvokeKind::Virtual&&kind!=native_call_graph::InvokeKind::Interface)||!extent(declaration,layout_.size))return deny("call-graph-dispatch-source-window-unavailable");
 method_metadata::Identity identity{};uint16_t slot=0;
 if(!method_metadata::ReadIdentity(layout_,declaration,layout_.size,&identity)||identity.dex_method_index==UINT32_MAX||
 (identity.access_flags&0x5000au)||!detail::MethodSlot(layout_,declaration,layout_.size,&slot))return deny("call-graph-dispatch-resolved-instance-declaration-invalid");
 void*owner=metadata_.MethodDeclaringClass(declaration);
 const auto&class_layout=metadata_.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 class_metadata::State owner_state{};
 if(!extent(owner,class_layout.size)||!extent(receiver,class_layout.size)||
 !class_metadata::ReadState(class_layout,owner,class_layout.size,&owner_state))return deny("call-graph-dispatch-declaring-class-unavailable");
 const bool interface=(owner_state.access_flags&0x200u)!=0;void*target=nullptr;
 if(kind==native_call_graph::InvokeKind::Interface&&!interface){
  // Fixed Class::FindVirtualMethodForInterface: a checked Object method takes
  // FindVirtualMethodForVirtual, never searches an arbitrary matching name.
  field_copy::CopyClassView object{};
  if(!(identity.access_flags&1u)||!metadata_.ClassIdentityVerified(owner,"Ljava/lang/Object;",nullptr,nullptr,nullptr,&object,nullptr,&read)||object.loader)
   return deny("call-graph-interface-noninterface-declaration-not-bootstrap-Object");
 }
 if(kind==native_call_graph::InvokeKind::Virtual||!interface){
  native_abstract_instances::VTableView view{};
  if(!vtables_.Ready()||!vtables_.ReadVTable(receiver,metadata_,&view,nullptr,&read))return deny("call-graph-current-calibrated-vtable-unavailable");
  size_t available=0;
  if(!read.extent(read.owner,view.table,&available)||!detail::Slot(view.table,available,view.payload,view.count,slot,&target))return deny("call-graph-virtual-slot-unavailable");
 }else{
  if((identity.access_flags&0x81000000u)==0x01000000u)return deny("call-graph-interface-declaration-is-copied");
  native_interface_graph::View view{};
  if(!native_interface_graph::ReadView(metadata_,receiver,&view,nullptr,&read))return deny("call-graph-current-interface-table-unavailable");
  void*table=metadata_.Reference(receiver,offsetof(class_metadata::Class11,iftable));size_t found=0;
  for(size_t i=0;i<view.Count();++i){void*actual=nullptr;
   if(!native_interface_graph::InterfaceAt(metadata_,view,i,&actual,&read))return deny("call-graph-current-interface-table-changed");
   if(actual!=owner)continue;if(++found!=1)return deny("call-graph-interface-domain-duplicate");
   const size_t offset=abi::arm64::android_common::kIfTableData+(i*2+1)*4;
   if(!extent(table,offset+4))return deny("call-graph-interface-method-array-slot-unmapped");
   void*array=metadata_.Reference(table,offset);size_t available=0;
   field_copy::CopyClassView array_class{};
   if(!array||!read.extent(read.owner,array,&available)||available<16||
    !metadata_.ClassIdentityVerified(metadata_.Reference(array,0),"[J",nullptr,nullptr,nullptr,&array_class,nullptr,&read)||array_class.loader||
    !native_interface_dispatch::ReadPointerSlot(array,available,slot,&target))return deny("call-graph-interface-method-slot-unavailable");
  }
  if(found!=1)return deny("call-graph-interface-declaration-outside-receiver-domain");
 }
 method_metadata::Identity target_identity{};
 if(!extent(target,layout_.size)||!method_metadata::ReadIdentity(layout_,target,layout_.size,&target_identity)||
 target_identity.dex_method_index==UINT32_MAX||(target_identity.access_flags&0x5040au))
  return deny("call-graph-dispatch-target-not-invokable");
 *out={declaration,owner,receiver,target,slot};return true;
}
}
