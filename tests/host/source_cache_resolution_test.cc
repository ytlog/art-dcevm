#include "platform/common/source_cache_resolution.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace artdcevm;
using namespace source_cache_resolution;
namespace fixed=abi::arm64::android_common;
struct Fixture final:Roots {
 uint32_t cache=0x1000,klass=0x2000,loader=0x3000;
 uint32_t actual_cache=0x1000,actual_loader=0x3000;
 uint64_t field_key=1,method_key=3,type_key=2;
 void* field_value=reinterpret_cast<void*>(0x4440);
 void* method_value=reinterpret_cast<void*>(0x5550);
 bool exception=false,foreign_field=false,foreign_method=false,foreign_type=false,move=false;
 mutable unsigned checks=0,descriptions=0;unsigned calls=0;
 Witness witness()const{return {&cache,&klass,&loader,1026,1025,2051,reinterpret_cast<void*>(0x8440),reinterpret_cast<void*>(0x9550)};}
 bool Snapshot(const Witness&w,void**out,std::string*error)const override {
  ++checks;
  if(exception||w.cache_slot!=&cache||w.class_slot!=&klass||w.loader_slot!=&loader||cache!=actual_cache||loader!=actual_loader){if(error)*error="namespace/exception refused";return false;}
  *out=reinterpret_cast<void*>(uintptr_t(klass));return true;
 }
 std::string DescribeField(const Witness&,void*returned)const override{
  ++descriptions;assert(!exception&&returned==reinterpret_cast<void*>(0x8888));
  return " member=outside-public-Class-native-arrays";
 }
}*active;
void Check(void*l,const void*c,const void*d){assert(l==active&&c==&active->cache&&d==&active->loader);++active->calls;}
void* Field(void*l,uint32_t id,const void*c,const void*d){Check(l,c,d);assert(id==1025);active->field_key=id;active->field_value=reinterpret_cast<void*>(0x8440);return active->foreign_field?reinterpret_cast<void*>(0x8888):active->field_value;}
void* Method(void*l,uint32_t id,const void*c,const void*d){Check(l,c,d);assert(id==2051);active->method_key=id;active->method_value=reinterpret_cast<void*>(0x9550);return active->foreign_method?nullptr:active->method_value;}
abi::arm64::android11::ReleaseObjectPointer Type(void*l,fixed::CacheTypeIndex id,const void*c,const void*d){Check(l,c,d);assert(id.index==1026);active->type_key=id.index;if(active->move)active->klass=0x6000;return {active->foreign_type?uintptr_t(0x9000):uintptr_t(active->klass)};}
struct Hooks final:HookEngine {
 bool legacy=false,missing=false,throw_field=false;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*lib,const char*name)override{
  assert(!strcmp(lib,"libart.so"));
  if(!strcmp(name,"_ZN3art11ClassLinker13DoResolveTypeENS_3dex9TypeIndexENS_6HandleINS_6mirror8DexCacheEEENS3_INS4_11ClassLoaderEEE"))return reinterpret_cast<void*>(Type);
  if(!strcmp(name,"_ZN3art11ClassLinker15ResolveFieldJLSEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE"))return missing?nullptr:reinterpret_cast<void*>(throw_field?ThrowField:Field);
  if(!strcmp(name,"_ZN3art11ClassLinker15ResolveMethodIdEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE"))return legacy?nullptr:reinterpret_cast<void*>(Method);
  if(!strcmp(name,"_ZN3art11ClassLinker30ResolveMethodWithoutInvokeTypeEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE"))return legacy?reinterpret_cast<void*>(Method):nullptr;
  assert(false);return nullptr;
 }
 static void* ThrowField(void*l,uint32_t id,const void*c,const void*d){auto*r=Field(l,id,c,d);active->exception=true;return r;}
 void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}
 bool Remove(void*,std::string*)override{assert(false);return false;}
};
int main(){
 for(bool legacy:{false,true}){
  Fixture f;active=&f;Hooks hooks;hooks.legacy=legacy;Service service;std::string e;
  assert(Service::Bind(hooks,&f,&service,&e));
  // Cache warming was displaced by another public declaration in slot 1.
  assert(f.field_key==1&&f.field_value!=f.witness().field);
  assert(service.Resolve(f.witness(),f,&e)&&f.calls==3&&f.checks==4);
  assert(f.field_key==1025&&f.method_key==2051&&f.type_key==1026&&f.field_value==f.witness().field&&f.method_value==f.witness().method);
  f.move=true;assert(service.Resolve(f.witness(),f,&e)&&f.klass==0x6000);
  for(int bad=0;bad<6;++bad){
   Fixture failed;active=&failed;Hooks h;h.legacy=legacy;h.throw_field=bad==5;Service s;assert(Service::Bind(h,&failed,&s,&e));
   if(bad==0)failed.loader=0x3333;if(bad==1)failed.cache=0x1111;
   failed.foreign_field=bad==2;failed.foreign_method=bad==3;failed.foreign_type=bad==4;
   assert(!s.Resolve(failed.witness(),failed,&e));
   if(bad==2)assert(e.find("idx=1025 expected=")!=std::string::npos&&e.find("actual=0x8888")!=std::string::npos&&e.find("outside-public-Class-native-arrays")!=std::string::npos);
   assert(failed.descriptions==(bad==2?1u:0u));
   assert(failed.calls==(bad<2?0:bad==2||bad==5?1:bad==3?2:3));
  }
 }
 Hooks h;Fixture f;active=&f;Service s;std::string e;h.missing=true;assert(!Service::Bind(h,&f,&s,&e));
 h.missing=false;assert(!Service::Bind(h,nullptr,&s,&e));assert(Service::Bind(h,&f,&s,&e));
 auto w=f.witness();w.type_index=65535;assert(!s.Resolve(w,f,&e)&&f.calls==0);
 puts("PASS named original cache resolution, displaced slots, moving roots, exact namespace/identity and exception refusal");
}
