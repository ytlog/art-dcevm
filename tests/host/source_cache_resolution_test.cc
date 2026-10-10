#include "platform/common/source_cache_resolution.h"
#include "platform/common/native_hierarchy_cache.h"
#include "platform/common/method_cache_lookup.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include "platform/abi/arm64/common/suspend_all.h"
using namespace artdcevm;
using namespace source_cache_resolution;
namespace fixed=abi::arm64::android_common;
struct Fixture final:Roots {
 type_cache::Dual cache_object{};
 type_cache::Pair types[1024]{};
 method_cache::SequencePair fields[1024]{},methods[1024]{};
 Fixture(){cache_object.types=uintptr_t(types);cache_object.fields=uintptr_t(fields);cache_object.methods=uintptr_t(methods);
  fields[1].key.store(1);fields[1].method.store(0x4440);}

 uint32_t cache=0x1000,klass=0x2000,loader=0x3000;
 uint32_t actual_cache=0x1000,actual_loader=0x3000;
 uint64_t field_key=1,method_key=3,type_key=2;
 void* field_value=reinterpret_cast<void*>(0x4440);
 void* method_value=reinterpret_cast<void*>(0x5550);
 bool exception=false,foreign_field=false,foreign_method=false,foreign_type=false,move=false,class_field=false,move_in_snapshot=false,skip_field_cache=false;
 mutable unsigned checks=0,descriptions=0;unsigned calls=0;
 Witness witness()const{return {&cache,&klass,&loader,1026,1025,2051,reinterpret_cast<void*>(0x8440),reinterpret_cast<void*>(0x9550)};}
 bool Snapshot(const Witness&w,Objects*out,std::string*error)const override {
  ++checks;
  if(exception||w.cache_slot!=&cache||w.class_slot!=&klass||w.loader_slot!=&loader||cache!=actual_cache||loader!=actual_loader){if(error)*error="namespace/exception refused";return false;}
  if(move_in_snapshot&&checks==2)const_cast<Fixture*>(this)->klass=0x6000;
  *out={reinterpret_cast<void*>(uintptr_t(klass)),reinterpret_cast<void*>(uintptr_t(cache)),reinterpret_cast<void*>(uintptr_t(loader))};return true;
 }
 std::string DescribeField(const Witness&,void*returned)const override{
  ++descriptions;if(!returned)return " member=null";assert(!exception&&(returned==reinterpret_cast<void*>(0x8888)||(class_field&&returned==reinterpret_cast<void*>(uintptr_t(klass)))));
  return " member=outside-public-Class-native-arrays";
 }
}*active;
void Check(void*l,const void*c,const void*d){assert(l==active&&c==&active->cache&&d==&active->loader);++active->calls;}
void* Field(void*l,uint32_t id,const void*c,const void*d){Check(l,c,d);assert(id==1025);active->field_key=id;active->field_value=reinterpret_cast<void*>(0x8440);if(!active->skip_field_cache){active->fields[id%1024].key.store((uint64_t{2}<<32)|id);active->fields[id%1024].method.store(uintptr_t(active->field_value));}return active->class_field?reinterpret_cast<void*>(uintptr_t(active->klass)):active->foreign_field?reinterpret_cast<void*>(0x8888):active->field_value;}
void* Method(void*l,uint32_t id,const void*c,const void*d){Check(l,c,d);assert(id==2051);active->method_key=id;active->method_value=reinterpret_cast<void*>(0x9550);active->methods[id%1024].key.store((uint64_t{2}<<32)|id);active->methods[id%1024].method.store(uintptr_t(active->method_value));return active->foreign_method?nullptr:active->method_value;}
abi::arm64::android11::ReleaseObjectPointer Type(void*l,fixed::CacheTypeIndex id,const void*c,const void*d){Check(l,c,d);assert(id.index==1026);active->type_key=id.index;if(active->move)active->klass=0x6000;active->types[id.index%1024]={active->klass,id.index};return {active->foreign_type?uintptr_t(0x9000):uintptr_t(active->klass)};}
void* ClassField(void*l,abi::arm64::android11::ReleaseObjectPointer c,
 abi::arm64::android11::ReleaseObjectPointer d,abi::arm64::android11::ReleaseObjectPointer loader,uint32_t index,bool is_static){
 assert(l==active&&c.reference==active->klass&&d.reference==active->cache&&loader.reference==active->loader);
 if(is_static){++active->calls;return nullptr;}return Field(l,index,&active->cache,&active->loader);
}
struct NativeFixture final:NativeWindow {bool native=true;bool OrdinaryNative()const noexcept override{return native;}} native_fixture;
struct Hooks final:HookEngine {
 bool legacy=false,missing=false,throw_field=false,aliased=false,class_specific=true;
 void* jls_replacement=nullptr,*find_replacement=nullptr;unsigned installs=0,removals=0;bool fail_install=false,fail_remove=false;
 static void Construct(void*,const char*,bool){}
 static void Destroy(void*){}
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*lib,const char*name)override{
  assert(!strcmp(lib,"libart.so"));
  if(!strcmp(name,"_ZN3art16ScopedSuspendAllC2EPKcb"))return reinterpret_cast<void*>(Construct);
  if(!strcmp(name,"_ZN3art16ScopedSuspendAllD2Ev"))return reinterpret_cast<void*>(Destroy);
  if(!strcmp(name,"_ZN3art11ClassLinker13DoResolveTypeENS_3dex9TypeIndexENS_6HandleINS_6mirror8DexCacheEEENS3_INS4_11ClassLoaderEEE"))return reinterpret_cast<void*>(Type);
  if(!strcmp(name,"_ZN3art11ClassLinker17FindResolvedFieldENS_6ObjPtrINS_6mirror5ClassEEENS1_INS2_8DexCacheEEENS1_INS2_11ClassLoaderEEEjb"))return !class_specific||missing?nullptr:aliased?reinterpret_cast<void*>(Type):reinterpret_cast<void*>(throw_field?ThrowClassField:ClassField);
  if(!strcmp(name,"_ZN3art11ClassLinker15ResolveFieldJLSEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE"))return missing?nullptr:aliased?reinterpret_cast<void*>(Type):reinterpret_cast<void*>(throw_field?ThrowField:Field);
  if(!strcmp(name,"_ZN3art11ClassLinker15ResolveMethodIdEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE"))return legacy?nullptr:reinterpret_cast<void*>(Method);
  if(!strcmp(name,"_ZN3art11ClassLinker30ResolveMethodWithoutInvokeTypeEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE"))return legacy?reinterpret_cast<void*>(Method):nullptr;
  assert(false);return nullptr;
 }
 static void* ThrowClassField(void*l,abi::arm64::android11::ReleaseObjectPointer c,abi::arm64::android11::ReleaseObjectPointer d,abi::arm64::android11::ReleaseObjectPointer loader,uint32_t index,bool is_static){auto*r=ClassField(l,c,d,loader,index,is_static);active->exception=true;return r;}
 static void* ThrowField(void*l,uint32_t id,const void*c,const void*d){auto*r=Field(l,id,c,d);active->exception=true;return r;}
 void* Install(void* target,void* replacement,void** original,std::string*)override{
  ++installs;if(fail_install&&installs==2)return nullptr;
  *original=target;
  if(target==reinterpret_cast<void*>(Field)){jls_replacement=replacement;return &jls_replacement;}
  assert(target==reinterpret_cast<void*>(ClassField));find_replacement=replacement;return &find_replacement;
 }
 bool Remove(void* token,std::string* error)override{++removals;*static_cast<void**>(token)=nullptr;if(fail_remove){if(error)*error="real removal refusal";return false;}return true;}
 static void Warm(void* raw){auto& h=*static_cast<Hooks*>(raw);
  if(h.jls_replacement){auto f=reinterpret_cast<fixed::ResolveCacheMember>(h.jls_replacement);assert(f(active,1025,&active->cache,&active->loader)==active->field_value);}
  if(h.find_replacement){auto f=reinterpret_cast<fixed::FindCacheField>(h.find_replacement);assert(f(active,{active->klass},{active->cache},{active->loader},1025,false)==active->field_value);}
 }

};

struct Mapper final:native_hierarchy_cache::SourceMapper {
 Fixture&f;explicit Mapper(Fixture&fixture):f(fixture){}
 void* Get(void*root)const noexcept override{return root==&f.cache?static_cast<void*>(&f.cache_object):root==&f.klass?reinterpret_cast<void*>(uintptr_t(f.klass)):nullptr;}
 bool Readable(const void*ptr,size_t size)const noexcept override{
  auto has=[&](const void*base,size_t n){auto a=uintptr_t(ptr),b=uintptr_t(base);return a>=b&&a-b<=n&&size<=n-(a-b);};
  return has(&f.cache_object,sizeof(f.cache_object))||has(f.types,sizeof(f.types))||has(f.fields,sizeof(f.fields))||has(f.methods,sizeof(f.methods));
 }
 bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&,std::string*)const override{return false;}
 bool Describe(const native_hierarchy_graph::Prepared&,native_hierarchy_cache::Source*,std::string*)const override{return false;}
 bool FindSource(const native_hierarchy_graph::Prepared&,uint32_t,void**,std::string*)const override{return false;}
 bool OwnsUnpublished(const native_hierarchy_graph::Prepared&,const native_hierarchy_cache::Source&)const noexcept override{return false;}
 bool Barrier(void*)const noexcept override{return false;}
 bool RefreshPrepared(native_hierarchy_graph::Prepared*)const noexcept override{return false;}
 bool VerifyFrozen(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,const native_hierarchy_cache::Source&)const noexcept override{return false;}
 bool VerifyCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{return false;}
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{assert(false);}
};
bool CalibrateCache(Fixture&f){Mapper mapper(f);native_hierarchy_cache::Owner owner;std::string error;
 native_hierarchy_cache::Witness w{1026,1025,2051,&f.klass,reinterpret_cast<void*>(0x8440),reinterpret_cast<void*>(0x9550)};
 return owner.Calibrate(mapper,&f.cache,sizeof(f.cache_object),{1033,1026,2056},w,&error);
}
namespace {
unsigned java_calls=0,local_deletes=0;int java_failure=0;
jobject JavaWriter(JNIEnv*,jclass type,jmethodID method,va_list arguments){
 assert(type==reinterpret_cast<jclass>(0x4000)&&method==reinterpret_cast<jmethodID>(0x7000));
 assert(va_arg(arguments,jobject)==reinterpret_cast<jobject>(active));++java_calls;
 // Independent public-original writer double. Production WarmPublic only
 // invokes JNI; it never writes these arrays or calls a private resolver.
 active->fields[1].key.store((uint64_t{2}<<32)|1);active->fields[1].method.store(0x4440);
 if(active->move)active->klass=0x6000;
 active->fields[1].key.store((uint64_t{4}<<32)|1025);active->fields[1].method.store(0x8440);
 active->methods[2051%1024].key.store((uint64_t{2}<<32)|2051);active->methods[2051%1024].method.store(0x9550);
 active->types[1026%1024]={active->klass,1026};
 if(java_failure==1)active->exception=true;
 if(java_failure==3)active->loader=0x3333;
 return java_failure==2?reinterpret_cast<jobject>(&active->cache):reinterpret_cast<jobject>(&active->klass);
}
jboolean SameJava(JNIEnv*,jobject a,jobject b){if(java_failure==4)active->exception=true;return a==b;}
void DeleteJava(JNIEnv*,jobject value){assert(value);++local_deletes;}
jboolean PendingJava(JNIEnv*){return active->exception;}
}
namespace {
uint64_t independent_longs[7]={0x1110,0x2220,0x3330,0x4440,0x5550,0x6660,0x7770};
unsigned long_reads=0,field_reads=0;int storage_failure=0;
jclass StorageType(JNIEnv*,jobject value){assert(value==reinterpret_cast<jobject>(0x1000));return reinterpret_cast<jclass>(0x4000);}
jclass StorageExpected(JNIEnv*,const char*name){assert(!strcmp(name,"java/lang/DexCache"));return reinterpret_cast<jclass>(0x4000);}
jfieldID StorageField(JNIEnv*,jclass c,const char*name,const char*desc){assert(c==reinterpret_cast<jclass>(0x4000)&&!strcmp(desc,"J"));++field_reads;
 for(size_t i=0;i<7;++i)if(!strcmp(name,PublicCacheFieldName(i))){if(storage_failure==1&&i==2)active->exception=true;return reinterpret_cast<jfieldID>(i+1);}assert(false);return nullptr;}
jobject StorageReflection(JNIEnv*,jclass c,jfieldID id,jboolean is_static){assert(c==reinterpret_cast<jclass>(0x4000)&&!is_static);return reinterpret_cast<jobject>(uintptr_t(id)+0x5000);}
jlong StorageLong(JNIEnv*,jobject c,jfieldID id){assert(c==reinterpret_cast<jobject>(0x1000));auto i=uintptr_t(id)-1;++long_reads;if(storage_failure==2&&i==3)active->exception=true;return independent_longs[i];}
jobject RetainStorage(void*,JNIEnv*,jobject value){return value;}
}
int main(){
 {
  for(bool model:{false,true}){bool legacy=true,dual=true;std::string error;
   for(size_t i=0;i<7;++i)assert(CheckPublicCacheField(i,(void*)0x1230,(void*)0x1230,PublicCacheFieldName(i),"J",2,PublicCacheFieldOffset(i,model),&legacy,&dual,&error));
   assert(model?dual&&!legacy:legacy&&!dual);
  }
  for(int bad=0;bad<5;++bad){bool legacy=true,dual=true;std::string error;
   assert(!CheckPublicCacheField(2,bad==0?(void*)0x9990:(void*)0x1230,(void*)0x1230,bad==1?"resolvedMethods":"resolvedFields",bad==2?"I":"J",bad==3?8:2,32,bad==4?nullptr:&legacy,&dual,&error));
   assert(legacy&&dual);
  }
 }
 {
  JNINativeInterface_ table{};table.GetObjectClass=StorageType;table.FindClass=StorageExpected;
  table.IsSameObject=SameJava;table.GetFieldID=StorageField;table.ToReflectedField=StorageReflection;
  table.GetLongField=StorageLong;table.ExceptionCheck=PendingJava;table.DeleteLocalRef=DeleteJava;JNIEnv env{&table};
  for(int failure=0;failure<3;++failure){Fixture f;active=&f;storage_failure=failure;java_failure=0;field_reads=long_reads=0;
   PublicCacheStorage got{};std::string error;
   assert(CapturePublicCacheStorage(&env,reinterpret_cast<jobject>(0x1000),nullptr,RetainStorage,&got,&error)==(failure==0));
   if(!failure){assert(long_reads==7&&field_reads==7);
    // Independently declared managed long values versus each fixed native ABI.
    for(bool dual:{false,true}){alignas(8) unsigned char native[112]{};
     for(size_t i=0;i<7;++i)std::memcpy(native+PublicCacheFieldOffset(i,dual),&independent_longs[i],8);
     assert(CheckPublicCacheStorage(got,native,&error));
     assert(!CheckPublicCacheStorage(got,native,&error,dual,!dual)); // Never mix field-offset and long-value source candidates.
     uint64_t bad=0xdead;std::memcpy(native+PublicCacheFieldOffset(4,dual),&bad,8);
     assert(!CheckPublicCacheStorage(got,native,&error));assert(error.find("resolvedMethods=5550/")!=std::string::npos);
    }
   }else if(failure==1)assert(field_reads==3&&long_reads==2);
   else assert(field_reads==4&&long_reads==4);
  }
 }
 {JNINativeInterface_ table{};table.CallStaticObjectMethodV=JavaWriter;table.IsSameObject=SameJava;
  table.DeleteLocalRef=DeleteJava;table.ExceptionCheck=PendingJava;JNIEnv env{&table};
  for(int failure=0;failure<5;++failure){Fixture f;active=&f;f.move=true;java_failure=failure;java_calls=0;local_deletes=0;
   JavaWarm call{reinterpret_cast<jclass>(0x4000),reinterpret_cast<jmethodID>(0x7000),reinterpret_cast<jobject>(&f),reinterpret_cast<jobject>(&f.klass)};std::string error;
   assert(!CalibrateCache(f));assert(WarmPublic(&env,call,f.witness(),f,&error)==(failure==0));
   assert(java_calls==1&&local_deletes==1&&f.calls==0); // No unknown direct leaf call.
   if(!failure)assert(f.klass==0x6000&&f.checks==2&&CalibrateCache(f));
   else assert(f.checks==(failure==3?2u:1u));
  }
  Fixture f;active=&f;java_failure=0;java_calls=0;JavaWarm missing{};std::string error;
  assert(!WarmPublic(&env,missing,f.witness(),f,&error)&&java_calls==0);
 }

 {Fixture f;active=&f;Hooks h;Service s;std::string error;
  assert(Service::BindDiagnostics(h,&f,&s,&error));native_fixture.native=false;
  assert(!s.TraceJavaWarm(h,native_fixture,1025,Hooks::Warm,&h,&error));
  assert(h.installs==0&&h.removals==0&&f.calls==0&&error.find("ordinary Native JNI")!=std::string::npos);
  native_fixture.native=true;
 }

 {Fixture f;active=&f;Hooks h;Service s;std::string error;
  assert(Service::BindDiagnostics(h,&f,&s,&error));
  assert(s.TraceJavaWarm(h,native_fixture,1025,Hooks::Warm,&h,&error));
  assert(h.installs==2&&h.removals==2&&!h.jls_replacement&&!h.find_replacement&&f.calls==2);
  // Independent original Java calls were exact, but direct mismatch still
  // refuses: observation is not a capability and cannot override an oracle.
  f.class_field=true;assert(!s.DiagnoseDirect(f.witness(),f,&error));
  assert(error.find("javaJls=1/")!=std::string::npos&&error.find("javaFind=1/")!=std::string::npos);
 }
 {Fixture f;active=&f;Hooks h;Service s;std::string error;
  assert(Service::BindDiagnostics(h,&f,&s,&error));h.fail_install=true;
  assert(!s.TraceJavaWarm(h,native_fixture,1025,Hooks::Warm,&h,&error));
  assert(h.installs==2&&h.removals==1&&f.calls==0&&!h.jls_replacement);
 }
 {Fixture f;active=&f;Hooks h;Service s;std::string error;
  assert(Service::BindDiagnostics(h,&f,&s,&error));
  assert(s.TraceJavaWarm(h,native_fixture,1025,[](void*){},nullptr,&error));
  f.class_field=true;assert(!s.DiagnoseDirect(f.witness(),f,&error));
  assert(error.find("javaJls=0/")!=std::string::npos&&error.find("javaFind=0/")!=std::string::npos);
 }


 {Fixture f;active=&f;Hooks h;Service s;std::string error;
  assert(Service::BindDiagnostics(h,&f,&s,&error));
  assert(s.TraceJavaWarm(h,native_fixture,1025,[](void* raw){Hooks::Warm(raw);active->exception=true;},&h,&error));
  assert(f.exception&&h.removals==2&&!h.jls_replacement&&!h.find_replacement);
  assert(!s.DiagnoseDirect(f.witness(),f,&error)&&f.calls==2); // No source call after exception.
 }
 for(bool class_specific:{false,true})for(bool legacy:{false,true}){
  Fixture f;active=&f;Hooks hooks;hooks.legacy=legacy;hooks.class_specific=class_specific;Service service;std::string e;
  assert(Service::BindDiagnostics(hooks,&f,&service,&e));
  // Cache warming was displaced by another public declaration in slot 1.
  assert(f.field_key==1&&f.field_value!=f.witness().field&&!CalibrateCache(f));
  assert(service.DiagnoseDirect(f.witness(),f,&e)&&f.calls==3&&f.checks==4&&CalibrateCache(f));
  f.fields[1].key.store((uint64_t{2}<<32)|1);assert(!CalibrateCache(f));
  assert(service.DiagnoseDirect(f.witness(),f,&e)&&CalibrateCache(f));
  assert(f.field_key==1025&&f.method_key==2051&&f.type_key==1026&&f.field_value==f.witness().field&&f.method_value==f.witness().method);
  f.move=true;assert(service.DiagnoseDirect(f.witness(),f,&e)&&f.klass==0x6000);
  for(int bad=0;bad<7;++bad){
   Fixture failed;active=&failed;Hooks h;h.legacy=legacy;h.class_specific=class_specific;h.throw_field=bad==5;Service s;assert(Service::BindDiagnostics(h,&failed,&s,&e));
   if(bad==0)failed.loader=0x3333;if(bad==1)failed.cache=0x1111;
   failed.foreign_field=bad==2;failed.foreign_method=bad==3;failed.foreign_type=bad==4;failed.class_field=bad==6;
   assert(!s.DiagnoseDirect(failed.witness(),failed,&e));
   if(bad==2)assert(e.find("idx=1025 expected=")!=std::string::npos&&e.find("pre=0x8888 post=0x8888 preExact=0")!=std::string::npos&&e.find("outside-public-Class-native-arrays")!=std::string::npos);
   if(bad==6)assert(e.find("pre=0x2000 post=0x2000 preExact=0")!=std::string::npos);
   assert(failed.descriptions==(bad==2||bad==6?1u:0u));
   assert(failed.calls==(bad<2?0:bad==2||bad==5||bad==6?1:bad==3?2:3));
  }
 }
 {Fixture moved;active=&moved;moved.move_in_snapshot=true;Hooks h;Service s;std::string e;
  assert(Service::BindDiagnostics(h,&moved,&s,&e)&&s.DiagnoseDirect(moved.witness(),moved,&e));
  assert(moved.klass==0x6000&&moved.field_value==moved.witness().field&&moved.calls==3);
 }
 {Fixture no_side_effect;active=&no_side_effect;no_side_effect.skip_field_cache=true;Hooks h;Service s;std::string e;
  assert(Service::BindDiagnostics(h,&no_side_effect,&s,&e)&&s.DiagnoseDirect(no_side_effect.witness(),no_side_effect,&e));
  assert(!CalibrateCache(no_side_effect)); // Exact return alone cannot grant format/table authority.
 }
 {Fixture bad_static;active=&bad_static;Hooks h;Service s;std::string e;auto w=bad_static.witness();w.field_static=true;
  assert(Service::BindDiagnostics(h,&bad_static,&s,&e)&&!s.DiagnoseDirect(w,bad_static,&e)&&bad_static.calls==1);
  assert(!CalibrateCache(bad_static));
 }
 {Fixture boot;active=&boot;boot.loader=boot.actual_loader=0;Hooks h;Service s;std::string e;
  assert(Service::BindDiagnostics(h,&boot,&s,&e)&&s.DiagnoseDirect(boot.witness(),boot,&e)&&CalibrateCache(boot));
 }
 Hooks h;Fixture f;active=&f;Service s;std::string e;h.missing=true;assert(!Service::BindDiagnostics(h,&f,&s,&e));
 h.missing=false;h.aliased=true;assert(!Service::BindDiagnostics(h,&f,&s,&e)&&f.calls==0);h.aliased=false;assert(!Service::BindDiagnostics(h,nullptr,&s,&e));assert(Service::BindDiagnostics(h,&f,&s,&e));
 auto w=f.witness();w.type_index=65535;assert(!s.DiagnoseDirect(w,f,&e)&&f.calls==0);
 {Fixture f;active=&f;Hooks h;Service s;std::string error;
  assert(Service::BindDiagnostics(h,&f,&s,&error));h.fail_remove=true;
  assert(!s.TraceJavaWarm(h,native_fixture,1025,Hooks::Warm,&h,&error));
  assert(h.removals==2&&error.find("observer removal refused")!=std::string::npos);
  assert(!s.TraceJavaWarm(h,native_fixture,1025,Hooks::Warm,&h,&error)&&error.find("restart process")!=std::string::npos);
 }
 puts("PASS named original cache resolution, displaced slots, moving roots, exact namespace/identity and exception refusal");
}
