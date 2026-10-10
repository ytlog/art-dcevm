#include "platform/common/native_interface_dispatch.h"
#include "platform/common/native_interface_graph.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android14/dex_cache.h"
#include "platform/common/native_boot_dex_identity.h"
#include "platform/abi/arm64/common/interface_table.h"
#include <cstring>
namespace artdcevm::native_interface_dispatch {
// AOSP ART ed6c006bd06ae060bd9698fd2cb25c4865512ec3:
// mirror/class-inl.h FindVirtualMethodForInterface; mirror/iftable-inl.h
// GetMethodArray; mirror/array-inl.h PointerArray::GetElementPtrSize; and
// art_method.h GetMethodIndex/IsCopied with libdexfile/dex/modifiers.h.
// These fixed declarations describe reads, never an SDK/BuildID permission.

namespace {
// Only these common source fields and scalar identity prefixes are shared.
// Android 16/17 complete Class tails differ and are never inferred here.
#define SHARED_CLASS_FIELD(field) static_assert(offsetof(class_metadata::Class11,field)==offsetof(class_metadata::Class16,field)&&offsetof(class_metadata::Class16,field)==offsetof(class_metadata::Class17,field))
SHARED_CLASS_FIELD(methods);
SHARED_CLASS_FIELD(class_loader);
SHARED_CLASS_FIELD(dex_cache);
SHARED_CLASS_FIELD(iftable);
#undef SHARED_CLASS_FIELD
static_assert(offsetof(class_metadata::Class16,dex_class_def_index)==offsetof(class_metadata::Class17,dex_class_def_index));
static_assert(offsetof(class_metadata::Class16,dex_type_index)==offsetof(class_metadata::Class17,dex_type_index));
// Fixed ownership prefixes, with no parser allocation or mutable-size-slot
// inference. Every namespace is read afresh through this exact stopped view.
bool Namespace(const modern_field_metadata::FieldMetadata&m,void*klass,
 const modern_field_metadata::ReadContext&c,evolution::MappedDexMetadata*out,
 field_copy::CopyClassView*named)noexcept{
 if(!klass||!out||!named||!c.owner||!c.extent)return false;
 const auto&layout=m.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 size_t extent=0;class_metadata::Identity identity{};
 if(!c.extent(c.owner,klass,&extent)||!class_metadata::ReadIdentity(layout,klass,extent,&identity)||identity.class_def_index<0||
    !m.ClassIdentity(klass,named,nullptr,&c)||!named->descriptor)return false;
 void*cache=m.Reference(klass,offsetof(class_metadata::Class11,dex_cache));
 constexpr size_t offset=offsetof(abi::arm64::android11::LegacyCache,dex_file);
 static_assert(offset==offsetof(abi::arm64::android14::DualCache,dex_file));
 if(!cache||!c.extent(c.owner,cache,&extent)||extent<offset+8)return false;
 uint64_t raw=0;std::memcpy(&raw,static_cast<const unsigned char*>(cache)+offset,8);
 const void*file=reinterpret_cast<void*>(uintptr_t(raw));dex_mapping_abi::Prefix prefix{};
 if(!file||!c.extent(c.owner,file,&extent)||extent<sizeof(prefix))return false;
 std::memcpy(&prefix,file,sizeof(prefix));
 size_t header_extent=0;
 if(!prefix.begin||!prefix.data_begin||!c.extent(c.owner,prefix.begin,&header_extent)||header_extent<112||
    !c.extent(c.owner,prefix.data_begin,&extent)||extent<prefix.data_size)return false;
 uint32_t size=0;std::memcpy(&size,prefix.begin+32,4);
 if(size<112||size>header_extent||size>UINTPTR_MAX-uintptr_t(prefix.begin)||prefix.data_size>UINTPTR_MAX-uintptr_t(prefix.data_begin))return false;
 *out={prefix.begin,size,prefix.data_begin,prefix.data_size,uint32_t(identity.class_def_index)};return true;
}
}
bool ReadPointerSlot(const void*array,size_t extent,uint32_t slot,void**out)noexcept{
 if(!array||!out||extent<16||extent>UINTPTR_MAX-uintptr_t(array)||uintptr_t(array)%8)return false;
 int32_t count=0;std::memcpy(&count,static_cast<const unsigned char*>(array)+8,4);
 if(count<0||size_t(count)>(extent-16)/8||slot>=uint32_t(count))return false;
 uint64_t target=0;std::memcpy(&target,static_cast<const unsigned char*>(array)+16+size_t(slot)*8,8);
 if(!target||target>UINTPTR_MAX)return false;*out=reinterpret_cast<void*>(uintptr_t(target));return true;
}
bool Source::Verify(const receiver_guard::CodeView&caller,uint32_t pc,void*receiver,void*callee,
 const modern_field_metadata::ReadContext&context,Match*out,const char**failure)const noexcept{
 auto deny=[&](const char*s){if(failure)*failure=s;return false;};if(failure)*failure=nullptr;
 const bool legacy=layout_.size==method_metadata::kAndroid11.size&&layout_.dex_method_index==method_metadata::kAndroid11.dex_method_index&&layout_.data==method_metadata::kAndroid11.data&&layout_.entry_point==method_metadata::kAndroid11.entry_point;
 const bool modern=layout_.size==method_metadata::kAndroid12Onward.size&&layout_.dex_method_index==method_metadata::kAndroid12Onward.dex_method_index&&layout_.data==method_metadata::kAndroid12Onward.data&&layout_.entry_point==method_metadata::kAndroid12Onward.entry_point;
 if((!legacy&&!modern)||!out||!receiver||!callee||!context.owner||!context.extent||!roots_.FinalRootWindow()||!metadata_.Calibrated())return deny("interface-edge-current-window-unavailable");
 auto mapped=[&](const void*p,size_t n){size_t extent=0;return p&&context.extent(context.owner,p,&extent)&&extent>=n;};
 receiver_guard::InvokeInstruction invoke{};
 if(!caller.instructions||caller.units>SIZE_MAX/2||!mapped(caller.instructions,caller.units*2)||
    !receiver_guard::DecodeInvoke(caller,pc,&invoke)||invoke.kind!=receiver_guard::InvokeKind::Interface)return deny("interface-edge-original-invoke-unproved");
 if(!mapped(caller.source_dex.begin,caller.source_dex.size)||!mapped(caller.source_dex.data_begin,caller.source_dex.data_size))return deny("interface-edge-caller-DEX-unmapped");
 if(!mapped(receiver,8)||!mapped(callee,layout_.size))return deny("interface-edge-receiver-or-callee-unmapped");
 method_metadata::Identity callee_identity{};
 if(!method_metadata::ReadIdentity(layout_,callee,layout_.size,&callee_identity)||callee_identity.dex_method_index==UINT32_MAX||!(callee_identity.access_flags&1u)||(callee_identity.access_flags&(0x2u|0x8u|0x400u|0x10000u|0x40000u)))return deny("interface-edge-callee-not-entered-instance-method");
 void*klass=metadata_.Reference(receiver,0);native_interface_graph::View view;
 if(!native_interface_graph::ReadView(metadata_,klass,&view,nullptr,&context))return deny("interface-edge-complete-IfTable-unproved");
 constexpr size_t table_offset=offsetof(class_metadata::Class11,iftable);
 void*table=metadata_.Reference(klass,table_offset);Match matched{};size_t matches=0;
 for(size_t i=0;i<view.Count();++i){
  void*interface=nullptr;if(!native_interface_graph::InterfaceAt(metadata_,view,i,&interface,&context))return deny("interface-edge-current-IfTable-changed");
  if(metadata_.Reference(interface,offsetof(class_metadata::Class11,class_loader)))continue; // Boot catalog owns only bootstrap namespaces.
  uint64_t raw=0;std::memcpy(&raw,static_cast<const unsigned char*>(interface)+offsetof(class_metadata::Class11,methods),8);
  auto*methods=reinterpret_cast<void*>(uintptr_t(raw));size_t extent=0;uint32_t count=0;
  if(!methods||!context.extent(context.owner,methods,&extent)||extent<8||uintptr_t(methods)%8)return deny("interface-edge-declaration-array-unmapped");
  std::memcpy(&count,methods,4);if(!layout_.size||size_t(count)>(extent-8)/layout_.size)return deny("interface-edge-declaration-array-truncated");
  uint32_t declared_virtuals=0;
  for(uint32_t j=0;j<count;++j){
   void*method=static_cast<unsigned char*>(methods)+8+size_t(j)*layout_.size;
   const uint32_t flags=method_metadata::ReadAccessFlags(method);
   const bool copied=(flags&0x81000000u)==0x01000000u;
   if(!(flags&(0x8u|0x2u|0x10000u))&&!copied){
    if(metadata_.MethodDeclaringClass(method)!=interface)return deny("interface-edge-foreign-original-declaration");
    ++declared_virtuals;
   }
  }
  for(uint32_t j=0;j<count;++j){
   void*method=static_cast<unsigned char*>(methods)+8+size_t(j)*layout_.size;
   if(metadata_.MethodDeclaringClass(method)!=interface)continue; // Inherited/copied methods cannot supply original interface slot.
   const auto*entry=catalog_.Lookup(method,roots_,context);if(!entry)continue;
   if(entry->modifiers&(0x8u|0x2u|0x10000u))continue;
   native_boot_aot_unaffected::DexIdentity ids(caller.source_dex);
   if(!ids.Matches(invoke.index,entry->declaring_signature,entry->name,entry->descriptor))continue;
   method_metadata::Identity identity{};
   if(!method_metadata::ReadIdentity(layout_,method,layout_.size,&identity)||identity.dex_method_index==UINT32_MAX||(identity.access_flags&0xffffu)!=(uint32_t(entry->modifiers)&0xffffu)||(identity.access_flags&0x40000u)||(identity.access_flags&0x81000000u)==0x01000000u)return deny("interface-edge-current-public-declaration-changed");
   evolution::MappedDexMetadata declaration_dex{},callee_dex{};field_copy::CopyClassView declaration_name{},callee_name{};
   if(!Namespace(metadata_,interface,context,&declaration_dex,&declaration_name)||entry->declaring_signature!=declaration_name.descriptor||
      !native_boot_aot_unaffected::DexIdentity(declaration_dex).Matches(identity.dex_method_index,entry->declaring_signature,entry->name,entry->descriptor))return deny("interface-edge-native-declaration-DEX-role-mismatch");
   void*callee_owner=metadata_.MethodDeclaringClass(callee);
   if(!Namespace(metadata_,callee_owner,context,&callee_dex,&callee_name)||
      !native_boot_aot_unaffected::DexIdentity(callee_dex).Matches(callee_identity.dex_method_index,callee_name.descriptor,entry->name,entry->descriptor))return deny("interface-edge-native-callee-DEX-role-mismatch");
   const size_t slot_offset=layout_.size==sizeof(method_metadata::Method11)?offsetof(method_metadata::Method11,method_index):offsetof(method_metadata::Method12,method_index);
   uint16_t slot=0;std::memcpy(&slot,static_cast<const unsigned char*>(method)+slot_offset,2);
   void*array=metadata_.Reference(table,abi::arm64::android_common::kIfTableData+(i*2+1)*4);
   if(!array||!context.extent(context.owner,array,&extent)||extent<16)return deny("interface-edge-method-array-unmapped");
   int32_t array_count=0;std::memcpy(&array_count,static_cast<const unsigned char*>(array)+8,4);
   if(array_count<0||uint32_t(array_count)!=declared_virtuals)return deny("interface-edge-method-array-count-differs-from-declarations");
   void*array_class=metadata_.Reference(array,0);field_copy::CopyClassView array_identity{};
   if(!metadata_.ClassIdentityVerified(array_class,"[J",nullptr,nullptr,nullptr,&array_identity,nullptr,&context)||array_identity.loader)return deny("interface-edge-method-array-not-bootstrap-long-array");
   void*target=nullptr;if(!ReadPointerSlot(array,extent,slot,&target)||target!=callee)return deny("interface-edge-current-slot-is-not-actual-callee");
   matched={interface,method,target,slot};if(++matches!=1)return deny("interface-edge-invoke-declaration-ambiguous");
  }
 }
 if(matches!=1||!roots_.FinalRootWindow())return deny("interface-edge-exact-invoke-declaration-unavailable");
 *out=matched;return true;
}
}
