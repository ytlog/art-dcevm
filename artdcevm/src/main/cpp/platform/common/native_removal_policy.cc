#include "platform/common/native_removal_policy.h"
#include "platform/common/jvmti_field_flags.h"
#include <algorithm>
namespace artdcevm::native_owned_removal {
namespace {
uint32_t JavaFlags(uint32_t flags){return (flags&0xffffu)|((flags&0x20000u)?0x20u:0u);}
bool Subset(const std::vector<evolution::Member>& old,const std::vector<evolution::Member>& next,bool fields){
  return std::all_of(next.begin(),next.end(),[&](const auto& member){
    const auto identity=[&](const auto& candidate){return candidate.name==member.name&&candidate.descriptor==member.descriptor;};
    // A repeated target is not a strict subset. Reject malformed duplicate
    // declarations before authorizing the native source entry.
    return std::count_if(next.begin(),next.end(),identity)==1&&
        std::count_if(old.begin(),old.end(),identity)==1&&
        std::any_of(old.begin(),old.end(),[&](const auto& candidate){
          return identity(candidate)&&(fields?jvmti_field_flags::Public(candidate.access)==jvmti_field_flags::Public(member.access):JavaFlags(candidate.access)==JavaFlags(member.access));
        });
  });
}
}
bool ValidateRemoval(const evolution::ClassSchema& old,const evolution::ClassSchema& next,bool allow_fields,std::string* error,bool require_removal){
  if(old.descriptor!=next.descriptor||old.superclass!=next.superclass||old.interfaces!=next.interfaces||
     JavaFlags(old.access)!=JavaFlags(next.access)||!Subset(old.fields,next.fields,true)||!Subset(old.methods,next.methods,false)||
     (!allow_fields&&old.fields.size()!=next.fields.size())||
     (require_removal&&old.fields.size()==next.fields.size()&&old.methods.size()==next.methods.size())){
    if(error)*error="Owned removal requires an unchanged class hierarchy and modifiers with a strict member subset";return false;
  }
  for(const auto& member:old.methods)if((member.name=="<init>"||member.name=="<clinit>")&&
      !std::any_of(next.methods.begin(),next.methods.end(),[&](const auto& kept){return kept.name==member.name&&kept.descriptor==member.descriptor;})){
    if(error)*error="Owned removal cannot delete a constructor or class initializer";return false;
  }
  if(error)error->clear();return true;
}
}
