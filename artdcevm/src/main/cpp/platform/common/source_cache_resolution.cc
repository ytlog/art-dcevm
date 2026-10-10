#include "platform/common/source_cache_resolution.h"
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <algorithm>
#include <array>
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android14/dex_cache.h"
#include "platform/abi/arm64/common/suspend_all.h"
namespace artdcevm::source_cache_resolution {
namespace {
bool Fail(std::string* e,const char* s){if(e)*e=s;return false;}
using namespace abi::arm64::android_common;
// Fixed source declarations. No observer touches JNI, managed objects, cache
// entries or original results; the recorded words are diagnostics, never roots.
struct WarmTrace {uint32_t index;unsigned jls=0,find=0;uintptr_t jls_owner=0,find_owner=0,jls_result=0,find_result=0;
 uintptr_t jls_cache=0,jls_loader=0,find_class=0,find_cache=0,find_loader=0;bool find_static=false;};
thread_local WarmTrace* java_warm=nullptr;
std::mutex trace_mutex;
bool trace_poisoned=false;
ResolveCacheMember original_jls=nullptr;
FindCacheField original_find=nullptr;
void* TraceJls(void* linker,uint32_t index,const void* cache,const void* loader){
 auto* result=original_jls(linker,index,cache,loader);
 if(java_warm&&java_warm->index==index){++java_warm->jls;java_warm->jls_owner=uintptr_t(linker);java_warm->jls_result=uintptr_t(result);java_warm->jls_cache=uintptr_t(cache);java_warm->jls_loader=uintptr_t(loader);}
 return result;
}
void* TraceFind(void* linker,abi::arm64::android11::ReleaseObjectPointer klass,abi::arm64::android11::ReleaseObjectPointer cache,
 abi::arm64::android11::ReleaseObjectPointer loader,uint32_t index,bool is_static){
 auto* result=original_find(linker,klass,cache,loader,index,is_static);
 if(java_warm&&java_warm->index==index){++java_warm->find;java_warm->find_owner=uintptr_t(linker);java_warm->find_result=uintptr_t(result);java_warm->find_class=klass.reference;java_warm->find_cache=cache.reference;java_warm->find_loader=loader.reference;java_warm->find_static=is_static;}
 return result;
}
}
bool WarmPublic(JNIEnv* env,const JavaWarm& call,const Witness& witness,const Roots& roots,std::string* error){
 if(!env||!call.bootstrap||!call.method||!call.input||!call.expected_class)return Fail(error,"Public cache original Java witness inputs incomplete");
 if(env->ExceptionCheck())return Fail(error,"Public cache original Java witness entered with pending exception");
 Objects current;if(!roots.Snapshot(witness,&current,error))return false;
 jobject returned=env->CallStaticObjectMethod(call.bootstrap,call.method,call.input);
 // DeleteLocalRef is safe cleanup with a pending original exception. Do not
 // perform identity JNI or another source observer until the exception check.
 if(env->ExceptionCheck()){if(returned)env->DeleteLocalRef(returned);return Fail(error,"Public cache original Java writer raised exception");}
 if(!returned)return Fail(error,"Public cache original Java writer returned no Class");
 const bool same=env->IsSameObject(returned,call.expected_class);
 const bool pending=env->ExceptionCheck();env->DeleteLocalRef(returned);
 if(pending||!same)return Fail(error,"Public cache original Java writer changed exact Class identity");
 // JNI restored its incoming Runnable state; reborrow all strong Pool roots
 // after the Java invocation's possible allocation/GC before native Calibrate.
 return roots.Snapshot(witness,&current,error);
}
namespace {
using Legacy=abi::arm64::android11::LegacyCache;using Dual=abi::arm64::android14::DualCache;
struct PublicField {const char*name;size_t legacy,dual;};
const PublicField public_fields[]={
 {"dexFile",offsetof(Legacy,dex_file),offsetof(Dual,dex_file)},
 {"resolvedCallSites",offsetof(Legacy,call_sites),offsetof(Dual,call_sites)},
 {"resolvedFields",offsetof(Legacy,fields),offsetof(Dual,fields)},
 {"resolvedMethodTypes",offsetof(Legacy,method_types),offsetof(Dual,method_types)},
 {"resolvedMethods",offsetof(Legacy,methods),offsetof(Dual,methods)},
 {"resolvedTypes",offsetof(Legacy,types),offsetof(Dual,types)},
 {"strings",offsetof(Legacy,strings),offsetof(Dual,strings)}};
}
const char* PublicCacheFieldName(size_t i){return i<7?public_fields[i].name:nullptr;}
size_t PublicCacheFieldOffset(size_t i,bool dual){return i<7?(dual?public_fields[i].dual:public_fields[i].legacy):SIZE_MAX;}
bool CheckPublicCacheField(size_t i,void*declaring,void*expected,const char*name,const char*descriptor,uint32_t flags,uint32_t offset,bool*legacy,bool*dual,std::string*error){
 if(i>=7||!expected||declaring!=expected||!name||!descriptor||std::strcmp(name,public_fields[i].name)||std::strcmp(descriptor,"J")||(flags&8u)||!legacy||!dual)
  return Fail(error,"Public DexCache reflected Field differs from exact native declaring Class/name/type/storage");
 *legacy&=offset==public_fields[i].legacy;*dual&=offset==public_fields[i].dual;return true;
}
bool CapturePublicCacheStorage(JNIEnv*env,jobject cache,void*owner,RetainPublicRoot retain,PublicCacheStorage*out,std::string*error){
 if(!env||!cache||!retain||!out||env->ExceptionCheck())return Fail(error,"Public DexCache storage JNI inputs or exception rejected");
 struct Local {JNIEnv*env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}};
 jclass type=env->GetObjectClass(cache);Local type_scope{env,type};
 if(!type||env->ExceptionCheck())return Fail(error,"Public DexCache Class unavailable");
 jclass expected=env->FindClass("java/lang/DexCache");Local expected_scope{env,expected};
 if(!expected||env->ExceptionCheck())return Fail(error,"Public DexCache bootstrap Class unavailable");
 const bool same=env->IsSameObject(type,expected);
 if(env->ExceptionCheck()||!same)return Fail(error,"Public cache differs from exact bootstrap DexCache Class");
 out->strong_class=retain(owner,env,type);
 if(!out->strong_class||env->ExceptionCheck())return Fail(error,"Public DexCache Class retention rejected");
 for(size_t i=0;i<7;++i){
  auto id=env->GetFieldID(type,public_fields[i].name,"J");
  if(!id||env->ExceptionCheck())return Fail(error,"Public DexCache exact long field unavailable");
  jobject reflected=env->ToReflectedField(type,id,JNI_FALSE);Local reflected_scope{env,reflected};
  if(!reflected||env->ExceptionCheck())return Fail(error,"Public DexCache exact reflected field unavailable");
  out->reflected_fields[i]=retain(owner,env,reflected);
  if(!out->reflected_fields[i]||env->ExceptionCheck())return Fail(error,"Public DexCache Field retention rejected");
  out->values[i]=static_cast<uint64_t>(env->GetLongField(cache,id));
  if(env->ExceptionCheck())return Fail(error,"Public DexCache original long field read raised exception");
 }
 return true;
}
bool CheckPublicCacheStorage(const PublicCacheStorage&values,const void*cache,std::string*error){
 if(!values.strong_class||!cache)return Fail(error,"Public DexCache native source comparison lacks independent identity");
 bool legacy=true,dual=true;char evidence[512]{};size_t used=0;
 for(size_t position=0;position<7;++position){const size_t i=static_cast<size_t>(std::array<int,7>{2,4,5,0,1,3,6}[position]);uint64_t a=0,b=0;
  std::memcpy(&a,static_cast<const unsigned char*>(cache)+public_fields[i].legacy,8);
  std::memcpy(&b,static_cast<const unsigned char*>(cache)+public_fields[i].dual,8);
  legacy&=a==values.values[i];dual&=b==values.values[i];
  int n=std::snprintf(evidence+used,sizeof(evidence)-used," %s=%llx/%llx/%llx",public_fields[i].name,
   static_cast<unsigned long long>(values.values[i]),static_cast<unsigned long long>(a),static_cast<unsigned long long>(b));
  if(n>0)used+=std::min(static_cast<size_t>(n),sizeof(evidence)-used-1);
 }
 if(!legacy&&!dual){if(error)*error=std::string("Exact public DexCache long fields differ from fixed source declarations (JNI/legacy/dual):")+evidence;return false;}
 return true;
}
bool Service::BindObservation(HookEngine& hooks,void* linker,Service* out,std::string* error){
 if(!out||!linker)return Fail(error,"Cache Java observer actual ClassLinker owner unavailable");
 Service candidate;candidate.linker_=linker;
 candidate.field_specific_=reinterpret_cast<abi::arm64::android_common::FindCacheField>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kFindCacheField));
 candidate.field_=reinterpret_cast<abi::arm64::android_common::ResolveCacheMember>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kResolveCacheField));
 if(!candidate.field_specific_&&!candidate.field_)return Fail(error,"Cache Java observer named field source entries unavailable");
 *out=candidate;return true;
}
bool Service::TraceJavaWarm(HookEngine& hooks,const NativeWindow& native,uint32_t index,void(*invoke)(void*),void* context,std::string* error){
 if(!invoke||java_warm||!native.OrdinaryNative())return Fail(error,"Cache Java witness observer requires actual ordinary Native JNI");
 std::lock_guard<std::mutex> lock(trace_mutex);
 if(trace_poisoned)return Fail(error,"Cache Java witness prior observer removal failed; restart process");
 const auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 const auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!construct||!destroy)return Fail(error,"Cache Java witness observer suspension declarations unavailable");
 void* jls=hooks.FindSymbol("libart.so",kResolveCacheField);
 void* find=reinterpret_cast<void*>(field_specific_);
 if(jls&&jls==find)return Fail(error,"Cache Java witness different source entries alias");
 if(!jls&&!find)return Fail(error,"Cache Java witness observer source targets unavailable");
 void* jt=nullptr,*ft=nullptr;
 if(!native.OrdinaryNative())return Fail(error,"Cache Java observer lost ordinary Native owner before installation");
 {SuspendAll scope{};construct(&scope,"artdcevm cache original Java observer",false);
  original_jls=nullptr;original_find=nullptr;
  if(jls)jt=hooks.Install(jls,reinterpret_cast<void*>(TraceJls),reinterpret_cast<void**>(&original_jls),error);
  if((!jls||jt)&&find)ft=hooks.Install(find,reinterpret_cast<void*>(TraceFind),reinterpret_cast<void**>(&original_find),error);
  destroy(&scope);
 }
 if(!native.OrdinaryNative()){trace_poisoned=true;return Fail(error,"Cache Java observer installation did not restore ordinary Native owner; restart process");}
 const bool installed=(!jls||(jt&&original_jls))&&(!find||(ft&&original_find));
 WarmTrace trace{index};
 if(installed){java_warm=&trace;invoke(context);java_warm=nullptr;}
 if(!native.OrdinaryNative()){trace_poisoned=true;return Fail(error,"Cache original Java invocation did not restore ordinary Native owner; restart process");}
 bool removed=true;
 {SuspendAll scope{};construct(&scope,"artdcevm cache Java observer removal",false);
  std::string removal;
  if(ft&&!hooks.Remove(ft,&removal))removed=false;
  if(jt&&!hooks.Remove(jt,&removal))removed=false;
  destroy(&scope);
  if(!native.OrdinaryNative()){trace_poisoned=true;return Fail(error,"Cache Java observer removal did not restore ordinary Native owner; restart process");}
  if(!removed){trace_poisoned=true;if(error)*error="Cache Java witness observer removal refused: "+removal;}
 }
 char facts[320];std::snprintf(facts,sizeof(facts)," javaJls=%u/%p/%p/%p/%p javaFind=%u/%p/%p/%p/%p/%p/%d expectedThis=%p",
  trace.jls,reinterpret_cast<void*>(trace.jls_owner),reinterpret_cast<void*>(trace.jls_result),reinterpret_cast<void*>(trace.jls_cache),reinterpret_cast<void*>(trace.jls_loader),
  trace.find,reinterpret_cast<void*>(trace.find_owner),reinterpret_cast<void*>(trace.find_result),reinterpret_cast<void*>(trace.find_class),reinterpret_cast<void*>(trace.find_cache),reinterpret_cast<void*>(trace.find_loader),trace.find_static,linker_);
 java_trace_=facts;
 return installed&&removed;
}
bool Service::BindDiagnostics(HookEngine& h,void* linker,Service*out,std::string*e){
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
bool Service::DiagnoseDirect(const Witness&w,const Roots&r,std::string*e)const{
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
   *e=std::string(detail);
   // Attribute the address returned by the named loader itself. This is
   // evidence only: no derived address/offset or alias fallback is executed.
   Dl_info info{};
   if(dladdr(field_specific_?reinterpret_cast<void*>(field_specific_):reinterpret_cast<void*>(field_),&info)&&info.dli_sname){
    *e+=field_specific_?" target=FindResolvedField":" target=ResolveFieldJLS";
    if(info.dli_fname)*e+=" module="+std::string(info.dli_fname);
    char address[112];std::snprintf(address,sizeof(address)," base=%p entry=%p symbol=%p nameExact=%d",info.dli_fbase,field_specific_?reinterpret_cast<void*>(field_specific_):reinterpret_cast<void*>(field_),info.dli_saddr,!std::strcmp(info.dli_sname,field_specific_?abi::arm64::android_common::kFindCacheField:abi::arm64::android_common::kResolveCacheField));*e+=address;
   }
   else *e+=" target-symbol=unattributed";
   *e+=java_trace_+r.DescribeField(w,field);
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
