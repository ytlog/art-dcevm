#include "platform/common/native_interface_dispatch_domain.h"
#include "platform/common/native_interface_dispatch.h"
#include "platform/common/native_boot_dex_identity.h"
#include <cstring>

namespace artdcevm::native_interface_dispatch_domain {
bool Source::Verify(void*caller,uint32_t pc,const modern_field_metadata::ReadContext&read,
                    void**declaration,const char**failure)const noexcept {
 const auto deny=[&](const char*reason){if(failure)*failure=reason;return false;};
 if(failure)*failure=nullptr;
 const auto layout_matches=[&](const method_metadata::Layout&expected){return methods_.size==expected.size&&
  methods_.dex_method_index==expected.dex_method_index&&methods_.data==expected.data&&methods_.entry_point==expected.entry_point;};
 if(!layout_matches(method_metadata::kAndroid11)&&!layout_matches(method_metadata::kAndroid12Onward))
  return deny("interface-domain-source-method-declaration-unavailable");
 if(!declaration||!caller||!driver_||!read.owner||!read.extent||
    (!stop_.IsCollectedStop(self_)&&!stop_.IsFinalWindow(self_))||
    !loading_.Paused()||!loading_.OwnsDriver(driver_)||!roots_.FinalRootWindow()||!catalog_.Complete()||
    !metadata_.Calibrated())return deny("interface-domain-original-window-unavailable");
 const auto*graph=hierarchy_.Staged();
 if(!graph)
  return deny("interface-domain-complete-current-census-or-root-bijection-unproved");
 const auto*caller_entry=rooted_.Lookup(caller,roots_,read);
 if(!caller_entry)return deny("interface-domain-current-public-caller-unproved");
 void*caller_class=metadata_.MethodDeclaringClass(caller);
 evolution::MappedDexMetadata caller_namespace{};field_copy::CopyClassView caller_identity{};
 size_t caller_extent=0;method_metadata::Identity caller_method{};
 if(!read.extent(read.owner,caller,&caller_extent)||caller_extent<methods_.size||
    !method_metadata::ReadIdentity(methods_,caller,caller_extent,&caller_method)||caller_method.dex_method_index==UINT32_MAX||
    (caller_method.access_flags&0xffffu)!=(uint32_t(caller_entry->modifiers)&0xffffu)||
    (caller_method.access_flags&(0x100u|0x400u|0x40000u))||
    (caller_method.access_flags&0x81000000u)==0x01000000u||
    !native_interface_dispatch::ReadCurrentNamespace(metadata_,caller_class,read,&caller_namespace,&caller_identity)||
    caller_identity.loader||!caller_identity.descriptor||caller_entry->declaring_signature!=caller_identity.descriptor||
    !native_boot_aot_unaffected::DexIdentity(caller_namespace).Matches(caller_method.dex_method_index,
        caller_entry->declaring_signature,caller_entry->name,caller_entry->descriptor))
  return deny("interface-domain-caller-resolution-namespace-unproved");
 receiver_guard::CodeView code{};
 if(!code_.Code(self_,caller,&code,read)||!code.instructions||code.units>SIZE_MAX/2||
    code.source_dex.begin!=caller_namespace.begin||code.source_dex.size!=caller_namespace.size||
    code.source_dex.data_begin!=caller_namespace.data_begin||code.source_dex.data_size!=caller_namespace.data_size||
    code.source_dex.class_def_index!=caller_namespace.class_def_index||
    caller_entry->dex.size()!=code.units*2||
    (code.units&&std::memcmp(caller_entry->dex.data(),code.instructions,code.units*2)))
  return deny("interface-domain-current-immutable-caller-body-unproved");
 receiver_guard::InvokeInstruction invoke{};
 if(!receiver_guard::DecodeInvoke(code,pc,&invoke)||invoke.kind!=receiver_guard::InvokeKind::Interface)
  return deny("interface-domain-current-interface-invoke-unproved");
 native_boot_aot_unaffected::DexIdentity caller_ids(code.source_dex);
 void*matched=nullptr;size_t matches=0;
 for(const auto&entry:catalog_.Methods()){
  if(!caller_ids.Matches(invoke.index,entry.declaring_signature,entry.name,entry.descriptor))continue;
  if((entry.modifiers&0x1u)==0||(entry.modifiers&(0x2u|0x8u|0x100u|0x10000u)))continue;
  if(rooted_.Lookup(entry.native_method,roots_,read)!=&entry)
   return deny("interface-domain-declaration-public-root-unproved");
  size_t extent=0;method_metadata::Identity method{};
  if(!read.extent(read.owner,entry.native_method,&extent)||extent<methods_.size||
     !method_metadata::ReadIdentity(methods_,entry.native_method,extent,&method)||
     method.dex_method_index==UINT32_MAX||(method.access_flags&0xffffu)!=(uint32_t(entry.modifiers)&0xffffu)||
     (method.access_flags&0x40000u)||(method.access_flags&0x81000000u)==0x01000000u)
   return deny("interface-domain-current-original-declaration-unproved");
  void*interface=metadata_.MethodDeclaringClass(entry.native_method);
  evolution::MappedDexMetadata dex{};field_copy::CopyClassView identity{};
  if(!native_interface_dispatch::ReadCurrentNamespace(metadata_,interface,read,&dex,&identity)||
     identity.loader||!identity.descriptor||entry.declaring_signature!=identity.descriptor||
     !native_boot_aot_unaffected::DexIdentity(dex).Matches(method.dex_method_index,
         entry.declaring_signature,entry.name,entry.descriptor))
   return deny("interface-domain-current-declaration-namespace-unproved");
  if(!callers_.VerifyHierarchyDomain(plan_,*graph,interface,read))
   return deny("interface-domain-complete-current-census-or-root-bijection-unproved");
  if(!hierarchy_.VerifyInterfaceDisjoint(plan_,interface,read))
   return deny("interface-domain-affected-before-or-proposed-IfTable-not-disjoint");
  matched=entry.native_method;if(++matches!=1)return deny("interface-domain-declaration-ambiguous");
 }
 if(matches!=1||!stop_.IsStopped(self_)||!loading_.Paused()||!roots_.FinalRootWindow())
  return deny("interface-domain-exact-current-declaration-unavailable");
 *declaration=matched;return true;
}
bool Source::Observe(void*caller,uint32_t pc,const modern_field_metadata::ReadContext&read,
                     Certificate*out,const char**failure)const noexcept {
 if(!out){if(failure)*failure="interface-domain-certificate-storage-unavailable";return false;}
 out->Clear();void*declaration=nullptr;if(!Verify(caller,pc,read,&declaration,failure))return false;
 out->source_=this;out->caller_=caller;out->pc_=pc;out->declaration_=declaration;return true;
}
bool Source::Validate(const Certificate&receipt,const modern_field_metadata::ReadContext&read,
                      const char**failure)const noexcept {
 if(receipt.source_!=this||!receipt.caller_||!receipt.declaration_){if(failure)*failure="interface-domain-certificate-owner-unproved";return false;}
 void*current=nullptr;if(!Verify(receipt.caller_,receipt.pc_,read,&current,failure))return false;
 if(current!=receipt.declaration_){if(failure)*failure="interface-domain-declaration-changed";return false;}return true;
}
}
