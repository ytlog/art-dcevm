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
 s.field_specific_=reinterpret_cast<FindCacheField>(h.FindSymbol("libart.so",kFindCacheField));
 // Prefer the source's class-specific writer once the exact Class/namespace
 // is already independently known. Absence can use the older declared JLS
 // resolver, but no runtime failed return selects or authorizes a fallback.
 if(!s.field_specific_)s.field_=reinterpret_cast<ResolveCacheMember>(h.FindSymbol("libart.so",kResolveCacheField));
 s.method_=reinterpret_cast<ResolveCacheMember>(h.FindSymbol("libart.so",kResolveCacheMethodId));
 if(!s.method_)s.method_=reinterpret_cast<ResolveCacheMember>(h.FindSymbol("libart.so",kResolveCacheMethodLegacy));
 if(!s.type_||(!s.field_&&!s.field_specific_)||!s.method_)return Fail(e,"Named cache type/field/method resolution declarations incomplete");
 // The source declarations have different parameter/return contracts; an
 // alias cannot prove that the requested named original was bound correctly.
 const auto field_address=s.field_specific_?reinterpret_cast<void*>(s.field_specific_):reinterpret_cast<void*>(s.field_);
 if(reinterpret_cast<void*>(s.type_)==field_address||
    reinterpret_cast<void*>(s.type_)==reinterpret_cast<void*>(s.method_)||field_address==reinterpret_cast<void*>(s.method_))
  return Fail(e,"Named cache resolution targets alias different source declarations");
 *out=s;return true;
}
bool Service::Resolve(const Witness&w,const Roots&r,std::string*e)const{
 Objects current;
 if(!linker_||!type_||(!field_&&!field_specific_)||!method_||!w.cache_slot||!w.class_slot||!w.loader_slot||
    w.type_index>=UINT16_MAX||!w.field||!w.method)return Fail(e,"Cache resolution immutable witnesses incomplete");
 if(!r.Snapshot(w,&current,e))return false;
 // Original ART resolvers populate their own cache. No setters or manual
 // cache writes: an existing hashed-slot collision must be resolved normally.
 if(!current.klass||!current.cache)return Fail(e,"Cache resolution complete current object roots unavailable");
 auto* field=field_specific_?field_specific_(linker_,{uintptr_t(current.klass)},
  {uintptr_t(current.cache)},{uintptr_t(current.loader)},w.field_index,w.field_static):
  field_(linker_,w.field_index,w.cache_slot,w.loader_slot);
 // Preserve independent immediate evidence BEFORE any ART/JNI observer.
 // This does not grant identity: both pre/post values MUST equal the public
 // native oracle; any observed alteration remains a hard refusal.
 volatile uintptr_t field_before=reinterpret_cast<uintptr_t>(field);
 volatile bool field_exact_before=field==w.field;
 if(!r.Snapshot(w,&current,e))return false;
 if(!field_exact_before||field!=w.field||uintptr_t(field)!=field_before){
  char detail[192];std::snprintf(detail,sizeof(detail),"Named field resolution differs from public immutable DEX witness: idx=%u expected=%p pre=%p post=%p preExact=%d",w.field_index,w.field,reinterpret_cast<void*>(field_before),field,field_exact_before);
  if(e){
   *e=std::string(detail)+r.DescribeField(w,field);
   // Attribute the address returned by the named loader itself. This is
   // evidence only: no derived address/offset or alias fallback is executed.
   Dl_info info{};
   if(dladdr(field_specific_?reinterpret_cast<void*>(field_specific_):reinterpret_cast<void*>(field_),&info)&&info.dli_sname)
    *e+=" target="+std::string(info.dli_sname);
   else *e+=" target-symbol=unattributed";
  }
  return false;
 }
 auto* method=method_(linker_,w.method_index,w.cache_slot,w.loader_slot);
 volatile uintptr_t method_before=reinterpret_cast<uintptr_t>(method);
 volatile bool method_exact_before=method==w.method;
 if(!r.Snapshot(w,&current,e))return false;
 if(!method_exact_before||method!=w.method||uintptr_t(method)!=method_before){
  char detail[192];std::snprintf(detail,sizeof(detail),"Named method resolution differs from public immutable DEX witness: idx=%u expected=%p pre=%p post=%p preExact=%d",w.method_index,w.method,reinterpret_cast<void*>(method_before),method,method_exact_before);
  return Fail(e,detail);
 }
 // Resolve the type last, after member resolution's potential safepoints.
 const auto type=type_(linker_,{static_cast<uint16_t>(w.type_index)},w.cache_slot,w.loader_slot);
 if(!r.Snapshot(w,&current,e))return false;
 if(!current.klass||type.reference!=uintptr_t(current.klass))return Fail(e,"Named type resolution differs from current rooted public Class");
 return true;
}
}
