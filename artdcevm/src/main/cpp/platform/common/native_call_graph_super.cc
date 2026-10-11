#include "platform/common/native_call_graph_super.h"
#include "platform/common/runtime_service_calibration.h"
#include "platform/common/native_source_class_loading.h"
#include "platform/common/native_interface_dispatch.h"
#include "platform/common/native_interface_graph.h"
#include <unordered_set>
#include <optional>
#include "platform/common/native_dex_identity.h"
#include "platform/common/readable_mapping_scope.h"
namespace artdcevm::native_call_graph_super {
namespace {
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
constexpr char kInterfaceSuper[]="_ZN3art6mirror5Class34FindVirtualMethodForInterfaceSuperEPNS_9ArtMethodENS_11PointerSizeE";
}
bool Resolver::Bind(HookEngine&hooks,JNIEnv*env,std::string*error){
 if(runtime_)return (runtime_slot_&&*runtime_slot_==runtime_)||Fail(error,"call-graph-super-runtime-owner-changed");
 if(!metadata_.Calibrated()||!vtables_.Ready())return Fail(error,"call-graph-super-source-metadata-not-calibrated");
 const bool old=layout_.size==method_metadata::kAndroid11.size&&layout_.dex_method_index==method_metadata::kAndroid11.dex_method_index;
 const bool current=layout_.size==method_metadata::kAndroid12Onward.size&&layout_.dex_method_index==method_metadata::kAndroid12Onward.dex_method_index;
 if(!old&&!current)return Fail(error,"call-graph-super-method-layout-not-calibrated");
 runtime_services::Snapshot observed;
 if(!runtime_services::CalibrateRuntimeServices(hooks,env,&observed,error))return false;
 auto singleton=static_cast<void**>(hooks.FindSymbol("libart.so","_ZN3art7Runtime9instance_E"));
 auto resolve=reinterpret_cast<TypeResolver>(hooks.FindSymbol("libart.so",abi::arm64::android17::kResolveMethodClass));
 if(!singleton||*singleton!=observed.runtime||!observed.runtime||!observed.receivers.class_linker||!resolve)
  return Fail(error,"call-graph-super-original-type-resolver-or-owner-unavailable");
 interface_super_=reinterpret_cast<InterfaceSuper>(hooks.FindSymbol("libart.so",kInterfaceSuper));
 runtime_=observed.runtime;runtime_slot_=singleton;linker_=observed.receivers.class_linker;resolve_type_=resolve;return true;
}
bool Resolver::ResolveOriginal(const runnable_jni::Context&context,receiver_guard::SourceProducer&source,
 void*caller,const receiver_guard::CodeView&staged,const std::vector<unsigned char>&bytes,
 const native_call_graph::CallSite&site,void*resolved,const modern_field_metadata::ReadContext&initial_read,
 Observation*out,std::string*error)const{
 if(out)*out={};
 auto deny=[&](const char*why){return Fail(error,why);};
 if(!out||!runtime_slot_||*runtime_slot_!=runtime_||!resolve_type_||!context.env||!context.self||
 !resolved||!initial_read.owner||!initial_read.extent||!metadata_.Calibrated()||!vtables_.Ready()||
 native_source_class_loading::Run::Current()||!runnable_jni::Bridge::BorrowOriginalScope(context)||
 context.env->ExceptionCheck()||site.instruction.kind!=receiver_guard::InvokeKind::Super)
  return deny("call-graph-super-original-prepause-owner-unavailable");
 auto access=source.Access();receiver_guard::CodeView code{};
 if(!access.current||!access.current(access.owner,context.self)||!source.Code(context.self,caller,&code,initial_read)||
 !native_call_graph_resolution::detail::Fresh(code,staged,bytes,site))return deny("call-graph-super-actual-caller-body-or-PC-changed");
 evolution::MappedDexMethodReference reference;
 if(!evolution::ReadMappedDexMethodReference(code.source_dex,site.instruction.index,&reference,error))return false;
 // DoResolveType<ArtMethod*> is explicitly instantiated by stock 11--17.
 // Original checked resolution already supplied access/ICCE checks. This
 // separate actual type resolution preserves the caller's loader namespace.
 const auto object=resolve_type_(linker_,{reference.class_index},caller);
 if(!object.reference){return deny(context.env->ExceptionCheck()?"call-graph-super-original-type-resolution-exception-pending":"call-graph-super-original-type-resolution-null-without-exception");}
 // Every original resolver may suspend/GC/unmap. The incoming view only
 // authenticates pre-resolution input. Recapture mappings in this immediate
 // no-JNI traversal, then discard them before any further original call.
 std::optional<method_identity::ScopedReadableMappings> mappings;
 auto refresh=[&](){mappings.emplace();return mappings->Ready();};
 const modern_field_metadata::ReadContext read{&mappings,[](void*owner,const void*address,size_t*extent){
  auto&view=*static_cast<std::optional<method_identity::ScopedReadableMappings>*>(owner);
  return view&&view->Extent(address,extent);
 }};
 if(!refresh())return deny("call-graph-super-fresh-readable-mappings-unavailable");
 // Do not perform JNI while borrowing the returned movable Class.
 if(!access.current(access.owner,context.self)||*runtime_slot_!=runtime_||
 !runnable_jni::Bridge::BorrowOriginalScope(context)||!source.Code(context.self,caller,&code,read)||
 !native_call_graph_resolution::detail::Fresh(code,staged,bytes,site))return deny("call-graph-super-owner-or-source-changed-after-type-resolution");
 void*referenced=reinterpret_cast<void*>(object.reference);
 field_copy::CopyClassView public_type{};
 if(!metadata_.ClassIdentityVerified(referenced,reference.declaring_class.c_str(),nullptr,nullptr,nullptr,&public_type,error,&read))
  return deny("call-graph-super-referenced-type-source-identity-unproved");
 size_t extent=0;method_metadata::Identity declaration{};uint16_t slot=0;
 if(!read.extent(read.owner,resolved,&extent)||!detail::Invokable(layout_,resolved,extent,true,&declaration)||
 !native_call_graph_dispatch::detail::MethodSlot(layout_,resolved,extent,&slot))return deny("call-graph-super-resolved-instance-method-unproved");
 // The declaration may be inherited/copied: name+signature comes from its own
 // actual native DEX, while referenced type remains the caller MethodId type.
 void*declaring=metadata_.MethodDeclaringClass(resolved);evolution::MappedDexMetadata declaration_dex{};
 field_copy::CopyClassView declaration_name{};bool matches=false;
 if(!native_interface_dispatch::ReadCurrentNamespace(metadata_,declaring,read,&declaration_dex,&declaration_name)||
 !evolution::ReadMappedDexMethodMatches(declaration_dex,declaration.dex_method_index,reference.name,reference.descriptor,&matches,error)||!matches)
  return deny("call-graph-super-resolved-native-DEX-name-signature-mismatch");
 void*caller_class=metadata_.MethodDeclaringClass(caller);
 const auto&class_layout=metadata_.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 class_metadata::State referenced_state{};
 if(!read.extent(read.owner,referenced,&extent)||!class_metadata::ReadState(class_layout,referenced,extent,&referenced_state)||
 !caller_class)return deny("call-graph-super-current-class-prefix-unproved");
 void*target=nullptr;void*super_class=nullptr;const bool interface=(referenced_state.access_flags&0x200u)!=0;
 if(interface){
  bool supports=false;if(!evolution::ReadMappedDexSupportsDefaultMethods(code.source_dex,&supports,error)||!supports)
   return deny("call-graph-super-caller-DEX-default-methods-unavailable");
  class_metadata::State owner_state{};
  if(!read.extent(read.owner,declaring,&extent)||!class_metadata::ReadState(class_layout,declaring,extent,&owner_state)||!(owner_state.access_flags&0x200u))
   return deny("call-graph-interface-super-resolved-declaration-is-not-interface");
  native_interface_graph::View interfaces;
  if(!native_interface_graph::ReadView(metadata_,caller_class,&interfaces,error,&read))return deny("call-graph-interface-super-caller-interface-domain-unproved");
  size_t found=0;for(size_t i=0;i<interfaces.Count();++i){void*actual=nullptr;
   if(!native_interface_graph::InterfaceAt(metadata_,interfaces,i,&actual,&read))return deny("call-graph-interface-super-current-domain-changed");
   if(actual==referenced)++found;
  }
  if(found!=1)return deny("call-graph-interface-super-referenced-type-outside-caller-domain");
  if(!interface_super_)return deny("call-graph-interface-super-named-default-selector-unavailable");
  // This original selector can allocate native scratch/handles; never call it
  // inside a paused/stopped window. Do not replace it with name-only selection.
  mappings.reset();
  target=detail::InterfaceTarget(interface_super_,referenced,resolved);
  if(!target)return deny(context.env->ExceptionCheck()?"call-graph-interface-super-original-exception-pending":"call-graph-interface-super-original-null-target");
  // The native selector owns handles and may allocate. Refresh the actual
  // referenced type and caller root after it; never reuse its input raw Class.
  const auto fresh_reference=resolve_type_(linker_,{reference.class_index},caller);
  if(!fresh_reference.reference)return deny(context.env->ExceptionCheck()?"call-graph-super-reference-refresh-exception-pending":"call-graph-super-reference-refresh-null");
  if(!refresh())return deny("call-graph-super-fresh-readable-mappings-unavailable");
  referenced=reinterpret_cast<void*>(fresh_reference.reference);
  caller_class=metadata_.MethodDeclaringClass(caller);
 }else{
  // Actual referenced Class must be in the caller's source superclass chain.
  std::unordered_set<void*>seen;bool found=false;void*current=caller_class;
  while(current){if(!seen.insert(current).second)return deny("call-graph-super-parent-chain-cycle");
   if(!read.extent(read.owner,current,&extent)||extent<offsetof(class_metadata::Class11,super_class)+4)
    return deny("call-graph-super-parent-chain-unmapped");
   current=metadata_.Reference(current,offsetof(class_metadata::Class11,super_class));if(current==referenced)found=true;
  }
  if(!found)return deny("call-graph-super-referenced-type-outside-caller-ancestors");
  super_class=metadata_.Reference(caller_class,offsetof(class_metadata::Class11,super_class));
  native_abstract_instances::VTableView table;
  if(!super_class||!vtables_.ReadVTable(super_class,metadata_,&table,error,&read)||
   !read.extent(read.owner,table.table,&extent)||
   !native_call_graph_dispatch::detail::Slot(table.table,extent,table.payload,table.count,slot,&target))
    return deny("call-graph-super-current-parent-vtable-slot-unproved");
 }
 method_metadata::Identity target_identity{};
 if(!target||!read.extent(read.owner,target,&extent)||!detail::Invokable(layout_,target,extent,false,&target_identity))
  return deny("call-graph-super-selected-target-not-invokable");
 evolution::MappedDexMetadata target_dex{};field_copy::CopyClassView target_name{};
 matches=false;if(!native_interface_dispatch::ReadCurrentNamespace(metadata_,metadata_.MethodDeclaringClass(target),read,&target_dex,&target_name)||
 !evolution::ReadMappedDexMethodMatches(target_dex,target_identity.dex_method_index,reference.name,reference.descriptor,&matches,error)||!matches)
  return deny("call-graph-super-selected-native-DEX-name-signature-mismatch");
 if(!access.current(access.owner,context.self)||*runtime_slot_!=runtime_||!runnable_jni::Bridge::BorrowOriginalScope(context)||
 !source.Code(context.self,caller,&code,read)||!native_call_graph_resolution::detail::Fresh(code,staged,bytes,site))
  return deny("call-graph-super-owner-or-caller-changed-after-selection");
 *out={caller,referenced,caller_class,super_class,resolved,target,code.source_dex.begin,site.dex_pc,reference.class_index,slot,interface};return true;
}
bool Resolver::ValidateObserved(const native_source_stop::Session&owner,void*self,
 const receiver_guard::CodeView&code,const std::vector<unsigned char>&bytes,
 const native_call_graph::CallSite&site,const evolution::MappedDexMethodReference&reference,
 const Observation&facts,const modern_field_metadata::ReadContext&read,const char**failure)const noexcept{
 auto deny=[&](const char*why){if(failure)*failure=why;return false;};if(failure)*failure=nullptr;
 if(!owner.IsStopped(self)||!runtime_slot_||*runtime_slot_!=runtime_||!resolve_type_||
 !metadata_.Calibrated()||!vtables_.Ready()||!read.owner||!read.extent||
 !facts.caller||!facts.caller_class||!facts.referenced_class||!facts.super_class||
 !facts.declaration||!facts.target)return deny("call-graph-super-retained-owner-or-roots-unavailable");
 if(facts.interface_super)return deny("call-graph-interface-super-retained-selector-dependencies-unproved");
 size_t code_extent=0;
 receiver_guard::InvokeInstruction invoke{};
 if(!code.instructions||!read.extent(read.owner,code.instructions,&code_extent)||
 code.units>code_extent/2||!code.source_dex.begin||facts.dex_namespace!=code.source_dex.begin||
 code.units>SIZE_MAX/2||bytes.size()!=code.units*2||std::memcmp(bytes.data(),code.instructions,bytes.size())||
 facts.dex_pc!=site.dex_pc||facts.type_index!=reference.class_index||
 !receiver_guard::DecodeInvoke(code,site.dex_pc,&invoke)||invoke.kind!=receiver_guard::InvokeKind::Super||
 !native_call_graph_resolution::detail::Same(invoke,site.instruction))
  return deny("call-graph-super-retained-caller-body-PC-or-reference-changed");
 size_t extent=0;method_metadata::Identity caller_identity{};
 if(!read.extent(read.owner,facts.caller,&extent)||!method_metadata::ReadIdentity(layout_,facts.caller,extent,&caller_identity)||
 caller_identity.dex_method_index==UINT32_MAX||(caller_identity.access_flags&0x40500u)||
 metadata_.MethodDeclaringClass(facts.caller)!=facts.caller_class)
  return deny("call-graph-super-retained-caller-declaring-root-changed");
 evolution::MappedDexMetadata caller_dex{};field_copy::CopyClassView caller_name{};
 if(!native_interface_dispatch::ReadCurrentNamespace(metadata_,facts.caller_class,read,&caller_dex,&caller_name)||
 caller_dex.begin!=code.source_dex.begin||caller_dex.size!=code.source_dex.size||
 caller_dex.data_begin!=code.source_dex.data_begin||caller_dex.data_size!=code.source_dex.data_size||
 !native_dex_identity::DexIdentity(caller_dex).Matches(invoke.index,reference.declaring_class,reference.name,reference.descriptor))
  return deny("call-graph-super-retained-caller-native-namespace-changed");
 const auto&classes=metadata_.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 class_metadata::State state{};field_copy::CopyClassView named{};
 if(!read.extent(read.owner,facts.referenced_class,&extent)||
 !class_metadata::ReadState(classes,facts.referenced_class,extent,&state)||
 (state.access_flags&0x200u)||!metadata_.ClassIdentity(facts.referenced_class,&named,nullptr,&read)||
 !named.descriptor||reference.declaring_class!=named.descriptor)
  return deny("call-graph-super-retained-referenced-class-changed");
 auto parent=[&](void*current,void**next){
  if(!current){*next=nullptr;return true;}
  size_t span=0;if(!read.extent(read.owner,current,&span)||span<offsetof(class_metadata::Class11,super_class)+4)return false;
  *next=metadata_.Reference(current,offsetof(class_metadata::Class11,super_class));return true;
 };
 void*head=nullptr;if(!parent(facts.caller_class,&head)||head!=facts.super_class)
  return deny("call-graph-super-retained-actual-super-root-changed");
 // Floyd detects any corrupt cycle without stopped-window allocation. Traverse
 // the entire chain, including the tail after the referenced ancestor.
 void*slow=head;void*fast=head;void*cursor=head;bool found=false;
 while(cursor){if(cursor==facts.referenced_class)found=true;
  if(!parent(cursor,&cursor)||!parent(slow,&slow)||!parent(fast,&fast)||!parent(fast,&fast))
   return deny("call-graph-super-retained-parent-chain-unmapped");
  if(slow&&slow==fast)return deny("call-graph-super-retained-parent-chain-cycle");
 }
 if(!found)return deny("call-graph-super-retained-reference-outside-ancestors");
 method_metadata::Identity declaration{},target{};uint16_t slot=0;
 if(!read.extent(read.owner,facts.declaration,&extent)||!detail::Invokable(layout_,facts.declaration,extent,true,&declaration)||
 !native_call_graph_dispatch::detail::MethodSlot(layout_,facts.declaration,extent,&slot)||slot!=facts.slot||
 !read.extent(read.owner,facts.target,&extent)||!detail::Invokable(layout_,facts.target,extent,false,&target))
  return deny("call-graph-super-retained-declaration-or-target-changed");
 for(const auto&method:{std::pair<void*,uint32_t>{facts.declaration,declaration.dex_method_index},{facts.target,target.dex_method_index}}){
  evolution::MappedDexMetadata dex{};field_copy::CopyClassView method_name{};
  if(!native_interface_dispatch::ReadCurrentNamespace(metadata_,metadata_.MethodDeclaringClass(method.first),read,&dex,&method_name)||
   !native_dex_identity::DexIdentity(dex).Matches(method.second,method_name.descriptor,reference.name,reference.descriptor))
    return deny("call-graph-super-retained-method-native-namespace-changed");
 }
 native_abstract_instances::VTableView table;void*selected=nullptr;
 if(!vtables_.ReadVTable(facts.super_class,metadata_,&table,nullptr,&read)||!read.extent(read.owner,table.table,&extent)||
 !native_call_graph_dispatch::detail::Slot(table.table,extent,table.payload,table.count,slot,&selected)||selected!=facts.target)
  return deny("call-graph-super-retained-parent-vtable-target-changed");
 return owner.IsStopped(self)&&*runtime_slot_==runtime_?true:deny("call-graph-super-retained-owner-changed");
}

}
