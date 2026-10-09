#include "platform/common/source_cache_resolution.h"
#include <cstdio>
#include <dlfcn.h>
namespace artdcevm::source_cache_resolution {
namespace {bool Fail(std::string* e,const char* s){if(e)*e=s;return false;}}
bool Service::Bind(HookEngine& h,void* linker,Service*out,std::string*e){
 if(!out||!linker)return Fail(e,"Cache resolution actual ClassLinker owner unavailable");
 using namespace abi::arm64::android_common;
 Service s;s.linker_=linker;
 s.type_=reinterpret_cast<ResolveCacheType>(h.FindSymbol("libart.so",kResolveCacheType));
 s.field_=reinterpret_cast<ResolveCacheMember>(h.FindSymbol("libart.so",kResolveCacheField));
 s.method_=reinterpret_cast<ResolveCacheMember>(h.FindSymbol("libart.so",kResolveCacheMethodId));
 if(!s.method_)s.method_=reinterpret_cast<ResolveCacheMember>(h.FindSymbol("libart.so",kResolveCacheMethodLegacy));
 if(!s.type_||!s.field_||!s.method_)return Fail(e,"Named cache type/field/method resolution declarations incomplete");
 // The source declarations have different parameter/return contracts; an
 // alias cannot prove that the requested named original was bound correctly.
 if(reinterpret_cast<void*>(s.type_)==reinterpret_cast<void*>(s.field_)||
    reinterpret_cast<void*>(s.type_)==reinterpret_cast<void*>(s.method_)||s.field_==s.method_)
  return Fail(e,"Named cache resolution targets alias different source declarations");
 *out=s;return true;
}
bool Service::Resolve(const Witness&w,const Roots&r,std::string*e)const{
 void* current=nullptr;
 if(!linker_||!type_||!field_||!method_||!w.cache_slot||!w.class_slot||!w.loader_slot||
    w.type_index>=UINT16_MAX||!w.field||!w.method)return Fail(e,"Cache resolution immutable witnesses incomplete");
 if(!r.Snapshot(w,&current,e))return false;
 // Original ART resolvers populate their own cache. No setters or manual
 // cache writes: an existing hashed-slot collision must be resolved normally.
 auto* field=field_(linker_,w.field_index,w.cache_slot,w.loader_slot);
 if(!r.Snapshot(w,&current,e))return false;
 if(field!=w.field){
  char detail[160];std::snprintf(detail,sizeof(detail),"Named field resolution differs from public immutable DEX witness: idx=%u expected=%p actual=%p",w.field_index,w.field,field);
  if(e){
   *e=std::string(detail)+r.DescribeField(w,field);
   // Attribute the address returned by the named loader itself. This is
   // evidence only: no derived address/offset or alias fallback is executed.
   Dl_info info{};
   if(dladdr(reinterpret_cast<void*>(field_),&info)&&info.dli_sname)
    *e+=" target="+std::string(info.dli_sname);
   else *e+=" target-symbol=unattributed";
  }
  return false;
 }
 auto* method=method_(linker_,w.method_index,w.cache_slot,w.loader_slot);
 if(!r.Snapshot(w,&current,e))return false;
 if(method!=w.method)return Fail(e,"Named method resolution differs from public immutable DEX witness");
 // Resolve the type last, after member resolution's potential safepoints.
 const auto type=type_(linker_,{static_cast<uint16_t>(w.type_index)},w.cache_slot,w.loader_slot);
 if(!r.Snapshot(w,&current,e))return false;
 if(!current||type.reference!=uintptr_t(current))return Fail(e,"Named type resolution differs from current rooted public Class");
 return true;
}
}
