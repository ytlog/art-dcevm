#pragma once
#include "evolution/class_schema.h"
#include <set>
#include <tuple>
namespace artdcevm::body_only {
// Public JVMTI MethodUtil::GetMethodModifiers exposes Java flags, maps
// declared-synchronized to synchronized, and hides ART's constructor bit.
// DEX-only bits are accepted only when their declared meaning is proven.
inline bool MethodFlags(const evolution::Member& m,uint32_t* out) {
 if(m.access&~0x3ffffu)return false;
 const bool constructor=m.name=="<init>"||m.name=="<clinit>";
 if((m.access&0x10000u)&&!constructor)return false;
 if(constructor&&(m.access&(evolution::kNative|evolution::kAbstract)))return false;
 if((m.access&evolution::kAbstract)&&(m.access&evolution::kNative))return false;
 *out=m.access&0xffffu;
 if(m.access&0x20000u)*out|=0x20u;
 return true;
}
inline bool SameDeclarations(const evolution::ClassSchema& live,
                             const evolution::ClassSchema& dex) {
 if(live.descriptor.empty()||live.descriptor!=dex.descriptor||
    live.superclass!=dex.superclass||live.access!=dex.access)return false;
 const std::set<std::string> a(live.interfaces.begin(),live.interfaces.end()),
                             b(dex.interfaces.begin(),dex.interfaces.end());
 if(a.size()!=live.interfaces.size()||b.size()!=dex.interfaces.size()||a!=b)return false;
 using Key=std::tuple<std::string,std::string,uint32_t>;
 auto members=[](const std::vector<evolution::Member>& input,bool methods,std::set<Key>* output){
  std::set<std::pair<std::string,std::string>> identities;
  for(const auto& m:input){uint32_t flags=m.access;
   if(m.name.empty()||m.descriptor.empty()||!identities.emplace(m.name,m.descriptor).second)return false;
   if(methods){if(!MethodFlags(m,&flags))return false;}else if(flags&~0xffffu)return false;
   output->emplace(m.name,m.descriptor,flags);
  }return true;
 };
 std::set<Key> lf,df,lm,dm;
 return members(live.fields,false,&lf)&&members(dex.fields,false,&df)&&lf==df&&
        members(live.methods,true,&lm)&&members(dex.methods,true,&dm)&&lm==dm;
}
}
