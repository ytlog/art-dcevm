#include "platform/common/native_field_factory_observer.h"
#include <exception>
namespace artdcevm::native_field_factory_observer {
namespace {constexpr const char* kFactory="_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadENS_6HandleINS0_5ClassEEEPNS_8ArtFieldEb";
constexpr const char* kThree="_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadEPNS_8ArtFieldEb";
constexpr const char* kLegacy="_ZN3art6mirror5Field18CreateFromArtFieldILNS_11PointerSizeE8ELb0EEENS_6ObjPtrIS1_EEPNS_6ThreadEPNS_8ArtFieldEb";
bool Fail(std::string* e,const char*t){if(e)*e=t;return false;}}
bool Service::Bind(HookEngine&h,const Bindings&b,std::string*e){
 if(target_||!b.owner||!b.current||!b.root)return Fail(e,"Field factory observer original Thread/root owners unavailable");
 target_=h.FindSymbol("libart.so",kFactory);four_argument_=target_!=nullptr;
 if(!target_)target_=h.FindSymbol("libart.so",kThree);
 if(!target_)target_=h.FindSymbol("libart.so",kLegacy);
 if(!target_)return Fail(e,"Named source Field factory absent");bindings_=b;return true;
}
bool Service::InstallAtExclusive(HookEngine&h,const exclusive_owned_roots::Context&c,std::string*e){
 if(poisoned_||!target_||token_||installed_||!c.self||c.self!=bindings_.current()||!c.roots||!c.count)return Fail(e,"Field factory observer requires unique actual exclusive rooted owner");
 installed_=this;original_=nullptr;token_=h.Install(target_,four_argument_?reinterpret_cast<void*>(Observe):reinterpret_cast<void*>(ObserveThree),&original_,e);
 if(!token_||!original_){
  bool removed=!token_||h.Remove(token_,e);if(!removed)poisoned_=true;
  if(removed){token_=nullptr;original_=nullptr;installed_=nullptr;}
  return Fail(e,poisoned_?"Field factory observer installation cleanup failed; restart process":"Field factory observer original installation incomplete");
 }
 return true;
}
bool Service::RemoveAtExclusive(HookEngine&h,const exclusive_owned_roots::Context&c,std::string*e){
 if(active_||!token_||installed_!=this||!c.self||c.self!=bindings_.current()||!c.roots||!c.count)return Fail(e,"Field factory observer removal lacks original exclusive rooted owner");
 if(!h.Remove(token_,e)){poisoned_=true;return Fail(e,"Field factory observer removal failed; restart process");}
 token_=nullptr;original_=nullptr;installed_=nullptr;return true;
}
bool Service::SetNormalizerAtExclusive(const Normalizer&n,const exclusive_owned_roots::Context&c,std::string*e){
 if(poisoned_||active_||!n.owner||!n.normalize||normalizer_.normalize||!target_||!c.self||c.self!=bindings_.current()||!c.roots||!c.count)return Fail(e,"Factory normalizer requires unique exact exclusive native owner");
 normalizer_=n;return true;
}
bool Service::Begin(void*self,const std::array<void*,2>&fields,std::string*e){
 if(poisoned_||installed_!=this||!token_||!original_||active_||!self||self!=bindings_.current()||!fields[0]||!fields[1]||fields[0]==fields[1])return Fail(e,"Field factory observation requires same real Thread and two exact distinct native targets");
 trace_={};for(size_t i=0;i<2;++i)trace_.entries[i].field=fields[i];self_=self;active_=this;return true;
}
Return Service::Observe(void*self,HandleCarrier handle,void*field,bool force){
 auto*s=installed_;if(!s||!s->original_)std::terminate();return s->Forward(self,handle,field,force);
}
Return Service::ObserveThree(void*self,void*field,bool force){
 auto*s=installed_;if(!s||!s->original_)std::terminate();return s->Forward(self,{nullptr},field,force);
}
Return Service::Forward(void*self,HandleCarrier handle,void*field,bool force){
 auto*s=this;
 Entry* entry=nullptr;
 if(active_==s&&self==s->self_&&s->bindings_.current()==self){
  for(auto&candidate:s->trace_.entries)if(candidate.field==field){entry=&candidate;break;}
 }
 if(entry){++entry->calls;entry->false_calls+=!force;entry->self=uintptr_t(self);entry->handle_slot=uintptr_t(handle.slot);
  void*object=nullptr;try{entry->root_before_valid=handle.slot&&s->bindings_.root(s->bindings_.owner,self,handle.slot,&object);}catch(...){entry->root_before_valid=false;}
  entry->root_before=entry->root_before_valid?uintptr_t(object):0;
 }
 void* normalized=field;
 const bool invoked=normalizer_.normalize!=nullptr;
 const bool accepted=!invoked||normalizer_.normalize(normalizer_.owner,self,handle,field,force,&normalized);
 if(entry){entry->normalization_invoked=invoked;entry->normalized_field=accepted?normalized:nullptr;}
 // A refused consumer owns its actual pending exception. Never invoke an ART
 // factory with a null/unproven target or manufacture a replacement result.
 Return returned{0};
 if(accepted&&(!invoked||normalized)){
  using Three=Return(*)(void*,void*,bool);
  returned=four_argument_?reinterpret_cast<Factory>(original_)(self,handle,normalized,force):reinterpret_cast<Three>(original_)(self,normalized,force);
 }
 if(entry){entry->result=returned.reference;entry->root_after_valid=false;entry->root_after=0;void*object=nullptr;
  if(returned.reference&&handle.slot)try{entry->root_after_valid=s->bindings_.root(s->bindings_.owner,self,handle.slot,&object);}catch(...){entry->root_after_valid=false;}
  entry->root_after=entry->root_after_valid?uintptr_t(object):0;
 }
 return returned;
}
bool Service::End(Trace*out,std::string*e){
 if(active_!=this||!out)return Fail(e,"Field factory observation has no matching original episode");
 *out=trace_;active_=nullptr;self_=nullptr;return true;
}
}
