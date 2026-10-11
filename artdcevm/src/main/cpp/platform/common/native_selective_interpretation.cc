#include "platform/common/native_selective_interpretation.h"
#include "platform/common/class_metadata_abi.h"
#include <cstring>
#include "platform/common/runtime_service_calibration.h"
#include <algorithm>
#include <limits>
#include <unordered_set>
namespace artdcevm::native_selective_interpretation {
namespace {
constexpr const char*kDeopt="_ZN3art15instrumentation15Instrumentation10DeoptimizeEPNS_9ArtMethodE";
constexpr const char*kQuery="_ZN3art15instrumentation15Instrumentation13IsDeoptimizedEPNS_9ArtMethodE";
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
bool Deny(const char**error,const char*why)noexcept{if(error)*error=why;return false;}
}
bool Service::CalibrateOwner(HookEngine&hooks,JNIEnv*env,std::string*error){
 if(runtime_){return (runtime_slot_&&*runtime_slot_==runtime_&&instrumentation_)||Fail(error,"selective-interpretation-calibrated-runtime-owner-changed");}
 runtime_services::Snapshot observed;
 if(!runtime_services::CalibrateRuntimeServices(hooks,env,&observed,error))return false;
 auto singleton=static_cast<void**>(hooks.FindSymbol("libart.so","_ZN3art7Runtime9instance_E"));
 if(!singleton||!*singleton||*singleton!=observed.runtime||!observed.instrumentation)
  return Fail(error,"selective-interpretation-independent-runtime-instrumentation-owner-unproved");
 runtime_=observed.runtime;instrumentation_=observed.instrumentation;runtime_slot_=singleton;return true;
}
bool Service::Contains(void*method)const noexcept{
 const auto key=reinterpret_cast<uintptr_t>(method);
 auto found=std::lower_bound(methods_.begin(),methods_.end(),key,[](const Method&m,uintptr_t p){return reinterpret_cast<uintptr_t>(m.method)<p;});
 return found!=methods_.end()&&found->method==method;
}
bool Service::Prepare(HookEngine&hooks,
 const method_identity::Calibration&methods,const modern_field_metadata::FieldMetadata&metadata,
 const bounded_managed_roots::Store&roots,const Method*selection,size_t count,std::string*error){
 if(prepared_&&(roots_!=&roots||generation_!=roots.Generation()))
  return Fail(error,"selective-interpretation-reprepare-owner-or-root-generation-changed");
 if(!runtime_||!runtime_slot_||*runtime_slot_!=runtime_||!instrumentation_)
  return Fail(error,"selective-interpretation-exact-instrumentation-owner-unproved");
 if(!metadata.Calibrated()||methods.identities.size()<3||methods.identities.size()!=methods.readable_extents.size())
  return Fail(error,"selective-interpretation-method-source-not-calibrated");
 if(!selection||!count)return Fail(error,"selective-interpretation-empty-method-closure");
 auto deopt=reinterpret_cast<Deopt>(hooks.FindSymbol("libart.so",kDeopt));
 auto query=reinterpret_cast<Query>(hooks.FindSymbol("libart.so",kQuery));
 if(!deopt||!query)return Fail(error,"selective-interpretation-named-method-deopt-query-unavailable");
 if(methods.model!=method_identity::Model::kMethod11&&methods.model!=method_identity::Model::kMethod12Onward)
  return Fail(error,"selective-interpretation-unknown-calibrated-method-layout");
 std::unordered_set<void*> unique;unique.reserve(count);
 for(size_t i=0;i<count;++i){
  if(!selection[i].method||selection[i].dex_index==UINT32_MAX||(selection[i].modifiers&0x500u))return Fail(error,"selective-interpretation-invalid-public-method");
  if(!unique.insert(selection[i].method).second)return Fail(error,"selective-interpretation-duplicate-method");
 }
 methods_.assign(selection,selection+count);
 std::sort(methods_.begin(),methods_.end(),[](const Method&a,const Method&b){return reinterpret_cast<uintptr_t>(a.method)<reinterpret_cast<uintptr_t>(b.method);});
 metadata_=&metadata;roots_=&roots;generation_=roots.Generation();
 layout_=methods.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 deopt_=deopt;query_=query;prepared_=true;return true;
}
bool Service::Window(const native_source_stop::Session&stop,void*self)const noexcept{
 return prepared_&&stop.IsStopped(self)&&runtime_slot_&&*runtime_slot_==runtime_&&instrumentation_&&
  roots_&&roots_->Generation()==generation_&&metadata_&&metadata_->Calibrated();
}
bool Service::Check(const Method&m,const modern_field_metadata::ReadContext&read,const char**failure)const noexcept{
 if(!read.owner||!read.extent)return Deny(failure,"selective-interpretation-current-read-context-missing");
 void*klass=nullptr;size_t extent=0;
 if(!roots_->Read(m.declaring_root,&klass)||!klass||!read.extent(read.owner,m.method,&extent)||
    !detail::Identity(layout_,m.method,extent,m.dex_index,m.modifiers))
  return Deny(failure,"selective-interpretation-native-abstract-obsolete-or-public-identity-refused");
 if(metadata_->MethodDeclaringClass(m.method)!=klass)
  return Deny(failure,"selective-interpretation-current-declaring-root-unproved");
 void*containing=nullptr;
 if(!roots_->Read(m.containing_root,&containing)||!containing)
  return Deny(failure,"selective-interpretation-containing-class-root-unproved");
 // Copied/default methods retain their interface declaring root but belong to
 // the implementing Class's array. These are independent rooted identities.
 const bool legacy=metadata_->StorageModel()==modern_field_metadata::Model::kSplit;
 const size_t flags=legacy?offsetof(class_metadata::Class11,access_flags):offsetof(class_metadata::Class16,access_flags);
 const size_t methods=legacy?offsetof(class_metadata::Class11,methods):offsetof(class_metadata::Class16,methods);
 for(void*owner:{klass,containing}){
  if(!read.extent(read.owner,owner,&extent)||extent<flags+4)
   return Deny(failure,"selective-interpretation-class-source-span-refused");
  const auto*bytes=static_cast<const unsigned char*>(owner);
  if(__atomic_load_n(reinterpret_cast<const uint32_t*>(bytes+flags),__ATOMIC_RELAXED)&0x40000u)
   return Deny(failure,"selective-interpretation-proxy-method-refused");
 }
 if(!read.extent(read.owner,containing,&extent)||extent<methods+8)
  return Deny(failure,"selective-interpretation-containing-class-source-span-refused");
 const auto*bytes=static_cast<const unsigned char*>(containing);
 uint64_t array=0;std::memcpy(&array,bytes+methods,8);
 if(!array||array%8||!read.extent(read.owner,reinterpret_cast<void*>(uintptr_t(array)),&extent)||extent<8)
  return Deny(failure,"selective-interpretation-current-method-array-unproved");
 if(!detail::Member(reinterpret_cast<const void*>(uintptr_t(array)),extent,m.method,layout_.size))
  return Deny(failure,"selective-interpretation-method-not-in-current-rooted-class-array");
 return true;
}
bool Service::Activate(const native_source_stop::Session&stop,void*self,const modern_field_metadata::ReadContext&read,Report*out)noexcept{
 if(!out)return false;*out={};out->selected=methods_.size();
 if(!Window(stop,self)){out->failure="selective-interpretation-actual-exclusive-stop-or-owner-unproved";return false;}
 // Check the complete selection before changing any entrypoint. Deoptimize has
 // an internal native set allocation, so failure after a successful call leaves
 // that method safely retained and the caller must not publish its mutation.
 for(const auto&m:methods_)if(!Check(m,read,&out->failure))return false;
 for(const auto&m:methods_){
  if(!Window(stop,self)){out->failure="selective-interpretation-owner-changed-during-activation";return false;}
  if(query_(instrumentation_,m.method)){++out->already_interpreted;continue;}
  deopt_(instrumentation_,m.method);++out->activated;out->requires_stack_drain=true;
  if(!query_(instrumentation_,m.method)){out->failure="selective-interpretation-original-deopt-did-not-retain-method";return false;}
 }
 out->requires_stack_drain=true;if(!Window(stop,self)){out->failure="selective-interpretation-owner-changed-after-activation";return false;}return true;
}
bool Service::ValidateRetained(const native_source_stop::Session&stop,void*self,const modern_field_metadata::ReadContext&read,Report*out)const noexcept{
 if(!out)return false;*out={};out->selected=methods_.size();out->requires_stack_drain=true;
 if(!Window(stop,self)){out->failure="selective-interpretation-retained-owner-or-stop-unproved";return false;}
 for(const auto&m:methods_){if(!Check(m,read,&out->failure))return false;
  if(!query_(instrumentation_,m.method)){out->failure="selective-interpretation-selected-method-left-deoptimized-set";return false;}
  ++out->already_interpreted;
 }
 if(!Window(stop,self)){out->failure="selective-interpretation-owner-changed-after-retained-query";return false;}return true;
}
}
