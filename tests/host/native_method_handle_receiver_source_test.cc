#include "native_receiver_handle_fixture.inc"
#include "platform/common/native_method_handle_receiver_source.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
namespace {
bool no_alloc=false;
struct Array {uint32_t klass=0,monitor=0;int32_t count=1;uint32_t item=1,item2=2;};
struct Sample {abi::arm64::android_common::MethodHandlePrefix handle{};receiver_guard::MethodTypeSourcePrefix type{};Array parameters{};int member=0,caller=0,declaring=0,parameter=0;};
std::array<Sample,9> samples;std::array<uint32_t,9> kinds{0,1,2,3,4,8,9,10,11};
bool readable=true,references=true,roots=true,visits=true,related=true;unsigned entries=0;
void SourceVisit(void*object,art::ReflectiveValueVisitor*v){++entries;for(auto&s:samples)if(object==&s.handle){art::ReflectionSourceInfo info;info.type=visits?3:4;info.object=object;if(s.handle.kind>=8)v->VisitField(reinterpret_cast<art::ArtField*>(&s.member),info);else v->VisitMethod(reinterpret_cast<art::ArtMethod*>(&s.member),info);return;}}
bool IsCurrent(void*,void*self)noexcept{return self==current;}
bool Extent(void*,void*self,const void*p,size_t*out)noexcept{if(!readable||self!=current)return false;for(auto&s:samples){if(p==&s.handle){*out=sizeof(s.handle);return true;}if(p==&s.type){*out=sizeof(s.type);return true;}if(p==&s.parameters){*out=sizeof(s.parameters);return true;}}return false;}
bool Ref(void*,void*self,void*object,size_t offset,void**out)noexcept{if(!references||self!=current)return false;for(auto&s:samples){if(object==&s.handle&&offset==16){*out=&s.type;return true;}if(object==&s.type&&offset==16){*out=&s.parameters;return true;}if(object==&s.parameters&&offset==12){*out=&s.caller;return true;}if(object==&s.parameters&&offset==16){*out=&s.parameter;return true;}}return false;}
bool Strong(void*,void*self,jobject root,void**out)noexcept{if(!roots||self!=current||!root)return false;*out=reinterpret_cast<void*>(root);return true;}
bool Entry(void*,void*self,void*h,uint32_t*k,void**m,bool*f)noexcept{return retirement::ReadMethodHandleReceiverEntry(self,h,k,m,f);}
bool Declaring(void*,void*self,void*m,bool field,void**out)noexcept{if(self!=current)return false;for(auto&s:samples)if(m==&s.member&&field==(s.handle.kind>=8)){*out=&s.declaring;return true;}return false;}
bool Special(void*,void*self,void*declaring,void*caller,bool*out)noexcept{if(self!=current)return false;assert(declaring==&samples[1].declaring&&caller==&samples[1].caller&&declaring!=caller);*out=related;return true;}
receiver_guard::MethodHandleSourceOracle Oracle(size_t n){auto&s=samples[n];receiver_guard::MethodHandleSourceOracle o{current,reinterpret_cast<jobject>(&s.handle),reinterpret_cast<jobject>(&s.type),reinterpret_cast<jobject>(&s.declaring),{},&s.member,kinds[n],kinds[n]>=8};if(s.parameters.count>0)o.parameter_classes.push_back(reinterpret_cast<jobject>(&s.caller));if(s.parameters.count>1)o.parameter_classes.push_back(reinterpret_cast<jobject>(&s.parameter));return o;}
}
void*operator new(size_t n){if(no_alloc)std::abort();if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
int main(){
 retirement::state.installed=true;retirement::state.field_kinds_calibrated=true;retirement::state.binding.current=Current;retirement::state.binding.visit=SourceVisit;retirement::state.binding.kind=abi::arm64::android_common::MethodHandleKind;
 for(size_t i=0;i<samples.size();++i){samples[i].handle.kind=kinds[i];samples[i].handle.target=uintptr_t(&samples[i].member);if(kinds[i]==3||kinds[i]==4||kinds[i]==10)samples[i].parameters.count=0;if(kinds[i]==9)samples[i].parameters.count=2;}
 const receiver_guard::MethodHandleSourceAccess access{nullptr,IsCurrent,Extent,Ref,Strong,Entry,Declaring,Special};
 receiver_guard::MethodHandleReceiverSource source(access);uint32_t kind=99;void*caller=reinterpret_cast<void*>(99);
 assert(!source.Seal()&&!source.Read(current,&samples[1].handle,&kind,&caller));
 auto one=Oracle(1);auto wrong=one;wrong.member=&samples[0].member;assert(!source.Observe(wrong));wrong=one;wrong.parameter_classes[0]=reinterpret_cast<jobject>(&samples[0].caller);assert(!source.Observe(wrong));wrong=one;wrong.type=reinterpret_cast<jobject>(&samples[0].type);assert(!source.Observe(wrong));wrong=one;wrong.declaring=reinterpret_cast<jobject>(&samples[0].declaring);assert(!source.Observe(wrong));wrong=one;wrong.thread=nullptr;assert(!source.Observe(wrong));
 related=false;assert(!source.Observe(one));related=true;
 samples[1].parameters.count=0;assert(!source.Observe(one));samples[1].parameters.count=1;
 samples[1].parameters.count=-1;assert(!source.Observe(one));samples[1].parameters.count=1;
 roots=false;assert(!source.Observe(one));roots=true;readable=false;assert(!source.Observe(one));readable=true;references=false;assert(!source.Observe(one));references=true;visits=false;assert(!source.Observe(one));visits=true;
 for(size_t i=0;i<samples.size();++i)assert(source.Observe(Oracle(i)));
 auto bad_setter=Oracle(6);bad_setter.parameter_classes[1]=reinterpret_cast<jobject>(&samples[0].parameter);assert(!source.Observe(bad_setter)&&!source.Seal());assert(source.Observe(Oracle(6)));
 assert(source.Seal()&&source.Proven()&&!source.Seal()&&!source.Observe(one));
 no_alloc=true;for(size_t i=0;i<samples.size();++i){kind=99;caller=reinterpret_cast<void*>(99);assert(source.Read(current,&samples[i].handle,&kind,&caller));assert(kind==kinds[i]&&caller==(i==1?&samples[i].caller:nullptr));}no_alloc=false;
 // Entry owner must be the genuine source-selected kind reader, not a custom
 // function accepting a supplied kind, and its source visitor must agree.
 retirement::state.binding.kind=Kind;assert(!source.Read(current,&samples[1].handle,&kind,&caller));retirement::state.binding.kind=abi::arm64::android_common::MethodHandleKind;
 retirement::state.field_kinds_calibrated=false;assert(!source.Read(current,&samples[1].handle,&kind,&caller));retirement::state.field_kinds_calibrated=true;
 readable=false;assert(!source.Read(current,&samples[1].handle,&kind,&caller));readable=true;references=false;assert(!source.Read(current,&samples[1].handle,&kind,&caller));references=true;visits=false;assert(!source.Read(current,&samples[1].handle,&kind,&caller));visits=true;
 samples[1].parameters.count=0;assert(!source.Read(current,&samples[1].handle,&kind,&caller));samples[1].parameters.count=1;
 assert(!source.Read(reinterpret_cast<void*>(1),&samples[1].handle,&kind,&caller));
 // Android11 source family shifts field kinds after its call-site transform;
 // INVOKE_SUPER remains 1. No SDK, target address or build ID selects it.
 retirement::state.binding.kind=abi::arm64::android11::MethodHandleKind;
 for(size_t i=5;i<samples.size();++i){++samples[i].handle.kind;uint32_t k=99;void*m=nullptr;bool f=false;assert(retirement::ReadMethodHandleReceiverEntry(current,&samples[i].handle,&k,&m,&f)&&k==kinds[i]&&m==&samples[i].member&&f);}
 samples[5].handle.kind=8;uint32_t k=99;void*m=nullptr;bool f=false;assert(!retirement::ReadMethodHandleReceiverEntry(current,&samples[5].handle,&k,&m,&f));
 std::puts("Actual MethodHandle entry visitor + source-selected kind + independent original type/parameter Class roots: nine families, specialCaller differs from target declaring Class, incomplete/foreign/malformed/no-owner rejection and allocation-free reads PASS; real device calibration still required");
}
