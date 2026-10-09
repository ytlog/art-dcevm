#include "platform/common/active_frame_recovery_proof.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace artdcevm;
using namespace active_frame_recovery_proof;
namespace {
Family family;int method_ids[3],thread,jit;void* active_method;
bool missing_context=false,bad_owner=false,bad_native=false,bad_fast=false,skip_inner=false,bad_map=false,bad_inline=false,inaccessible=false,wrong_geometry=false,bad_pc=false,bad_flag=false,physical_inlinees=false;
bool actual_physical=false,physical_alias=false,physical_wrong_order=false,nterp_header=false;unsigned active_index=0;
alignas(8) std::array<std::array<uint32_t,16>,3> physical_quick{};
uint32_t code_size=64;unsigned creates=0,resets=0,destroys=0,reference_fast11=0;
struct Context final:abi::arm64::context::Prefix {void Reset()override{++resets;}~Context()override{++destroys;}
 void FillCalleeSaves(uint8_t*,const abi::arm64::context::FrameInfo&)override{}
 void SetSP(uintptr_t)override{}void SetPC(uintptr_t)override{}void SetArg0(uintptr_t)override{}
 bool IsAccessibleGPR(uint32_t)override{return !inaccessible;}uintptr_t* GetGPRAddress(uint32_t)override{return nullptr;}
 uintptr_t GetGPR(uint32_t reg)override{assert(reg==7);return (uintptr_t{123}<<32)|123;}void SetGPR(uint32_t,uintptr_t)override{}
 bool IsAccessibleFPR(uint32_t)override{return !inaccessible;}uintptr_t GetFPR(uint32_t reg)override{assert(reg==7);return (uintptr_t{123}<<32)|123;}};
abi::arm64::context::Prefix* Create(){++creates;return missing_context?nullptr:new Context;}
alignas(8) uint32_t header[2]{};
alignas(8) std::array<uint32_t,16> quick{};
std::array<uint32_t,8> stack{};std::array<uint32_t,12> inlines{};uint32_t mask=63,roots=0xa8;
std::array<uint32_t,6> maps{};std::array<uint32_t,12> catalog{};
template<size_t N> void Table(abi::arm64::android11::BitTable<N>& t,void* data,size_t bits,uint32_t rows,unsigned width) {
 t.region={data,0,bits};t.rows=rows;for(size_t i=0;i<=N;++i)t.columns[i]=i*width;
}
template<size_t N> void Table(abi::arm64::android13::BitTable<N>& t,void* data,size_t bits,uint32_t rows,unsigned width) {
 t.region={data,0,bits};t.rows=rows;for(size_t i=0;i<=N;++i)t.columns[i]=i*width;
}
template<size_t N> void Table(abi::arm64::android17::BitTable<N>& t,void* data,size_t bits,uint32_t rows,unsigned width) {
 t.region={data,0,bits};t.rows=rows;t.row_bits=N*width;for(size_t i=0;i<N;++i)t.columns[i]=i*width;
}
template<class T> void Decode(T* info,const void* h) {
 assert(h==header);*info={};if constexpr(!std::is_same_v<T,abi::arm64::android11::CodeInfo>)info->code_size=code_size;info->flags=bad_flag?0:1;info->dex_regs=2;info->frame_size=4;
 if(actual_physical) {stack[2]=11+active_index;mask=3;roots=8;}
 Table(info->stack_maps,stack.data(),256,1,32);Table(info->inline_infos,inlines.data(),384,2,32);
 Table(info->dex_masks,&mask,6,1,6);Table(info->dex_maps,maps.data(),192,6,32);
 Table(info->dex_catalog,catalog.data(),384,6,32);Table(info->stack_masks,&roots,16,1,16);
}
template<class T> abi::arm64::android11::TableIterator Map(const T* info,uintptr_t pc,int32_t isa) {
 assert(pc==32 && isa==2);return {const_cast<void*>(static_cast<const void*>(&info->stack_maps)),bad_map?99u:0u};
}
abi::arm64::android11::TableIterator Map11(const abi::arm64::android11::CodeInfo* info,uint32_t pc,int32_t isa){return Map(info,pc,isa);}
bool Unmapped(const void*,size_t*){return false;}
bool JitContains(const void* j,const void* pc){assert(j==&jit);return !bad_owner && pc!=nullptr;}
bool HeaderContains(const void* h,uintptr_t pc){assert(h==header);return pc>reinterpret_cast<uintptr_t>(h);}
bool Nterp(const void* h){assert(h==header);return nterp_header;}
template<class V> void Construct(void* raw,void* t,void* c,abi::arm64::stack::WalkKind kind,bool check) {
 assert(t==&thread && c && check);auto* visitor=static_cast<V*>(raw);
 std::memset(static_cast<unsigned char*>(raw)+8,0,sizeof(V)-8);visitor->thread=t;visitor->context=c;visitor->kind=kind;
}
template<class V> void Walk(void* raw,bool transitions) {
 assert(!transitions);auto* v=static_cast<V*>(raw);assert(v->context && v->kind==abi::arm64::stack::WalkKind::kIncludeInlined);
 for(unsigned i=0;i<3;++i){if(skip_inner && i==0)continue;
  v->depth=i;v->quick_frame=reinterpret_cast<void**>(quick.data());v->header=header;
  v->pc=reinterpret_cast<uintptr_t>(header)+(family==Family::kAndroid11?8:4)+32;
  v->inline_frames.begin.row=0;v->inline_frames.end.row=(bad_inline && i==1)?2:2-i;
  if(physical_inlinees&&i<2)v->inline_frames.end.row=0;
  active_index=physical_wrong_order?i:2-i;active_method=&method_ids[active_index];
  if(actual_physical) {
   v->inline_frames.end.row=0;
   v->quick_frame=reinterpret_cast<void**>(physical_quick[physical_alias?0:active_index].data());
  }
  if(!v->VisitFrame())break;
 }
}
uint32_t DexPc(const void*,bool abort){assert(!abort);for(unsigned i=0;i<3;++i)if(active_method==&method_ids[i])return 10+i+(bad_pc?1:0);return UINT32_MAX;}
void* Method(const void*){return active_method;}
bool Slow(const void*,void* method,uint16_t reg,Kind kind,uint32_t* value) {
 assert((reg==0 && kind==Kind::kInt)||(reg==1 && kind==Kind::kReference));
 if(bad_native)return false;
 for(unsigned i=0;i<3;++i)if(method==&method_ids[i]){*value=quick[2+2*i+reg];return true;}
 return false;
}
bool SlowModern(const void* visitor,void* method,uint16_t reg,Kind kind,uint32_t* value,bool full) {
 assert(!full);return Slow(visitor,method,reg,kind,value);
}
bool Fast(const void*,NativeLocation location,uint32_t* value) {
 assert(location.kind==0 && location.value>=8 && location.value<=28);
 *value=(actual_physical?physical_quick[active_index][location.value/4]:quick[location.value/4])+(bad_fast?1:0);return true;
}
bool Fast11(const void* visitor,NativeLocation location,Kind kind,uint32_t* value){assert(kind==((location.value%8)==4?Kind::kReference:Kind::kInt));if(kind==Kind::kReference)++reference_fast11;return Fast(visitor,location,value);}
struct Finder {
 const char* missing=nullptr;
 template<class T> static void* Fn(T function){return reinterpret_cast<void*>(function);}
 void* operator()(const char* name)const {
  if(missing && std::strcmp(missing,name)==0)return nullptr;
  if(wrong_geometry && std::strcmp(name,abi::arm64::stack::kConstruct)==0)
   return family==Family::kAndroid12 || family==Family::kAndroid17?Fn(Construct<abi::arm64::stack::Visitor13>):Fn(Construct<abi::arm64::stack::Visitor12>);
  if(std::strstr(name,"GetDexPc"))return Fn(DexPc);
  if(std::strcmp(name,abi::arm64::context::kCreate)==0)return Fn(Create);
  if(std::strcmp(name,abi::arm64::android11::frame_registers::kSlow)==0)return Fn(Slow);
  if(std::strcmp(name,abi::arm64::android14::frame_registers::kSlow)==0)return Fn(SlowModern);
  if(std::strcmp(name,abi::arm64::android11::frame_registers::kFast)==0)return Fn(Fast11);
  if(std::strcmp(name,abi::arm64::android12::frame_registers::kFast)==0)return Fn(Fast);
  if(std::strstr(name,"ContainsPc"))return Fn(JitContains);
  if(std::strstr(name,"Header8Contains"))return Fn(HeaderContains);
  if(std::strstr(name,"IsNterpMethodHeader"))return Fn(Nterp);
  if(std::strcmp(name,abi::arm64::stack::kMethod)==0)return Fn(Method);
#define FAMILY_CASE(f,T,V) case Family::f: \
 if(std::strcmp(name,abi::arm64::stack::kConstruct)==0)return Fn(Construct<abi::arm64::stack::V>); \
 if(std::strstr(name,"WalkStack"))return Fn(Walk<abi::arm64::stack::V>); \
 if(std::strstr(name,"CodeInfoC2"))return Fn(Decode<abi::arm64::T::CodeInfo>); \
 if(std::strstr(name,"GetStackMapForNativePcOffset"))return Fn(Map<abi::arm64::T::CodeInfo>);break
  if(family==Family::kAndroid11 && std::strstr(name,"GetStackMapForNativePcOffset"))return Fn(Map11);
  switch(family){FAMILY_CASE(kAndroid11,android11,Visitor11);FAMILY_CASE(kAndroid12,android12,Visitor12);
   FAMILY_CASE(kAndroid13,android13,Visitor13);FAMILY_CASE(kAndroid14,android14,Visitor13);
   FAMILY_CASE(kAndroid15,android15,Visitor13);FAMILY_CASE(kAndroid16,android16,Visitor13);FAMILY_CASE(kAndroid17,android17,Visitor17);}
#undef FAMILY_CASE
  return nullptr;
 }
};
void Reset(){missing_context=bad_owner=bad_native=bad_fast=skip_inner=bad_map=bad_inline=false,inaccessible=false,wrong_geometry=false,bad_pc=false,bad_flag=false,physical_inlinees=false;creates=resets=destroys=reference_fast11=0;
 quick={};void* carrier=&method_ids[0];std::memcpy(quick.data(),&carrier,sizeof(carrier));
 actual_physical=physical_alias=physical_wrong_order=nterp_header=false;
 for(unsigned i=0;i<3;++i){quick[2+2*i]=123+111*i;quick[3+2*i]=0x11000+0x1000*i;}
 stack={};stack[2]=11;stack[4]=stack[5]=stack[6]=stack[7]=1;inlines={};inlines[0]=1;inlines[1]=12;inlines[7]=13;inlines[5]=5;inlines[11]=7;
 header[0]=16;header[1]=64;code_size=64;mask=63;roots=0xa8;for(unsigned i=0;i<6;++i){maps[i]=i+1;catalog[2*i]=1;catalog[2*i+1]=i+3;}
}
void Check(Family f){family=f;Reset();Service service;
 assert(!Service::Bind(f,Finder{abi::arm64::context::kCreate},&service));

 assert(!Service::Bind(f,Finder{f==Family::kAndroid14 || f==Family::kAndroid15 || f==Family::kAndroid16 || f==Family::kAndroid17 ? abi::arm64::android14::frame_registers::kSlow : abi::arm64::android11::frame_registers::kSlow},&service));
 const char* missing=nullptr;
 assert(!Service::Bind(f,Finder{abi::arm64::context::kCreate},&service,&missing));assert(std::strcmp(missing,abi::arm64::context::kCreate)==0);
 assert(Service::Bind(f,Finder{},&service));
 Witness witnesses[3][2];for(unsigned i=0;i<3;++i)witnesses[i][0]={0,Kind::kInt,quick[2+2*i]},witnesses[i][1]={1,Kind::kReference,quick[3+2*i]};
 Oracle oracle{{&method_ids[0],2,witnesses[0],2,10},{{&method_ids[1],2,witnesses[1],2,11},{&method_ids[2],2,witnesses[2],2,12}}};
 Oracle wrong_shape=oracle;wrong_shape.shape=static_cast<Shape>(99);
 decoded_frame_recovery::Location scratch[2];
 assert(service.Prove(&thread,&jit,wrong_shape,scratch,2).failure==Failure::kMissingOracle);auto run=[&](){return service.Prove(&thread,&jit,oracle,scratch,2);};
 auto result=run();assert(result.failure==Failure::kNone && result.frames==3 && result.references==3 && result.integers==3 && result.location_reads==6 && result.dex_pc_checks==3 && result.inline_flag_checks==2);
 assert(creates==1 && resets==1 && destroys==1);if(f==Family::kAndroid11)assert(reference_fast11==3);
 assert(service.Prove(&thread,&jit,oracle,scratch,2,Unmapped).failure==Failure::kTableMemory);
 // A legitimately optimized inner chain can still belong to inlinee1's own
 // physical body. Never count that callback as carrier's two-inline proof.
 for(unsigned physical_owner:{1u,2u}) {
  std::memcpy(quick.data(),&oracle.inlinees[physical_owner-1].method,sizeof(void*));
  auto partial=run();assert(partial.failure==Failure::kFrameIdentity && partial.frames==0 &&
      partial.inline_matches==1 && partial.physical_owner_mask==(physical_owner==1?2u:4u));
  assert(std::strcmp(partial.detail,"quick-frame-owner-is-not-independent-carrier")==0);
 }
 std::memcpy(quick.data(),&oracle.carrier.method,sizeof(void*));

 // Real nondebuggable compiler states: actual inline metadata without a vreg
 // map must never count as a recovered frame, nor as a wrong CodeItem size.
 stack[7]=0;auto omitted=run();
 assert(omitted.failure==Failure::kDecodedMap && omitted.frames==0 &&
        !omitted.register_range_observed && omitted.inline_matches==1);
 assert(std::strcmp(omitted.detail,"native-stack-map-has-no-dex-register-map")==0);
 stack[7]=1;
 auto outer_count=inlines[5],inner_count=inlines[11];inlines[5]=inlines[11]=3;
 auto empty_inline=run();assert(empty_inline.failure==Failure::kDecodedMap &&
   empty_inline.register_range_observed && empty_inline.native_carrier_registers==2 &&
   empty_inline.decoded_register_first==2 && empty_inline.decoded_register_count==0 &&
   empty_inline.expected_registers==2 && empty_inline.frames==0);
 assert(std::strcmp(empty_inline.detail,"decoded-vreg-count-does-not-match-actual-CodeItem")==0);
 inlines[5]=outer_count;inlines[11]=inner_count;

 physical_inlinees=true;auto separated=run();assert(separated.failure==Failure::kMissingFrames && separated.physical_matches==3 && separated.inline_matches==0 && separated.matched_mask==7 && separated.frames==1);assert(std::strcmp(separated.detail,"missing-actual-inlinees-physical-fixture-frames-observed")==0);physical_inlinees=false;
 Service inline_header;missing=nullptr;bool fallback_bound=Service::Bind(f,Finder{"_ZNK3art20OatQuickMethodHeader8ContainsEm"},&inline_header,&missing);
 if(f==Family::kAndroid11){
  assert(fallback_bound && !missing);
  auto prove=[&]{return inline_header.Prove(&thread,&jit,oracle,scratch,2);};
  assert(prove().failure==Failure::kNone);
  header[1]=0x80000040u;assert(prove().failure==Failure::kNone);
  header[1]=31;assert(prove().failure==Failure::kCodeOwnership);
  header[1]=UINT32_MAX;assert(prove().failure==Failure::kCodeOwnership);
  header[1]=64;header[0]=0;assert(prove().failure==Failure::kCodeOwnership);header[0]=16;
  using abi::arm64::android11::OptimizedHeaderContains;
  auto code=reinterpret_cast<uintptr_t>(header)+8;
  assert(OptimizedHeaderContains(header,8,code+64));
  assert(!OptimizedHeaderContains(header,7,code));
  assert(!OptimizedHeaderContains(header,8,code-1));
  assert(!OptimizedHeaderContains(header,8,code+65));
  assert(!OptimizedHeaderContains(reinterpret_cast<void*>(UINTPTR_MAX-3),8,UINTPTR_MAX));
 }
 else {assert(fallback_bound);auto recovered=inline_header.Prove(&thread,&jit,oracle,scratch,2);assert(recovered.failure==Failure::kNone && recovered.header_checks==3);code_size=31;auto outside=inline_header.Prove(&thread,&jit,oracle,scratch,2);assert(outside.failure==Failure::kCodeOwnership && std::strcmp(outside.detail,"decoded-code-size-does-not-contain-native-pc")==0);code_size=0;assert(inline_header.Prove(&thread,&jit,oracle,scratch,2).failure==Failure::kCodeOwnership);code_size=64;}
 missing_context=true;assert(run().failure==Failure::kMissingContext);missing_context=false;
 bad_owner=true;assert(run().failure==Failure::kCodeOwnership);bad_owner=false;
 bad_pc=true;assert(run().failure==Failure::kDexPc);bad_pc=false;
 ++inlines[7];assert(run().failure==Failure::kDexPc);--inlines[7];
 ++stack[2];assert(run().failure==Failure::kDexPc);--stack[2];
 bad_flag=true;assert(run().failure==Failure::kInlineFlag);bad_flag=false,physical_inlinees=false;
 auto pc=oracle.inlinees[0].expected_dex_pc;++oracle.inlinees[0].expected_dex_pc;assert(run().failure==Failure::kDexPc);oracle.inlinees[0].expected_dex_pc=pc;
 bad_map=true;assert(run().failure==Failure::kDecodedMap);bad_map=false;
 roots=0;assert(run().failure==Failure::kReferenceMask);roots=0xa8;
 bad_native=true;assert(run().failure==Failure::kNativeRead);bad_native=false;
 bad_fast=true;assert(run().failure==Failure::kValueMismatch);bad_fast=false;
 ++witnesses[2][1].expected;assert(run().failure==Failure::kValueMismatch);--witnesses[2][1].expected;
 skip_inner=true;assert(run().failure==Failure::kMissingFrames);skip_inner=false;
 bad_inline=true;assert(run().failure==Failure::kFrameIdentity);bad_inline=false,inaccessible=false,wrong_geometry=false,bad_pc=false,bad_flag=false,physical_inlinees=false;
 auto saved=witnesses[0][0].expected;witnesses[0][0].expected=witnesses[1][0].expected;
 assert(run().failure==Failure::kMissingOracle);witnesses[0][0].expected=saved;
 witnesses[0][1].expected=0;assert(run().failure==Failure::kMissingOracle);
 witnesses[0][1].expected=quick[3];wrong_geometry=true;
 Service wrong;assert(Service::Bind(f,Finder{},&wrong));
 auto rejected=wrong.Prove(&thread,&jit,oracle,scratch,2);
 assert(rejected.failure==Failure::kFrameIdentity && rejected.callbacks==0);
 assert(rejected.constructor_context_echo_mask==(f==Family::kAndroid12 || f==Family::kAndroid17?4u:2u));wrong_geometry=false,bad_pc=false,bad_flag=false,physical_inlinees=false;
 witnesses[0][1].expected=quick[3];
 Service fallback;assert(Service::Bind(f,Finder{f==Family::kAndroid11?abi::arm64::android11::frame_registers::kFast:abi::arm64::android12::frame_registers::kFast},&fallback));
 assert(fallback.Prove(&thread,&jit,oracle,scratch,2).failure==Failure::kNone);
 for(uint32_t kind=2;kind<=5;++kind) {
  catalog[0]=kind+1;catalog[1]=8;
  assert(fallback.Prove(&thread,&jit,oracle,scratch,2).failure==Failure::kNone);
  inaccessible=true;assert(fallback.Prove(&thread,&jit,oracle,scratch,2).failure==Failure::kNativeRead);inaccessible=false;
 }
 catalog[0]=2;catalog[1]=124;
 assert(fallback.Prove(&thread,&jit,oracle,scratch,2).failure==Failure::kNone); // constant123
 catalog[0]=1;catalog[1]=17;
 assert(fallback.Prove(&thread,&jit,oracle,scratch,2).failure==Failure::kDecodedMap); // out-of-frame64
 catalog[1]=3;
 // Separate physical callers must each own a distinct native quick frame,
 // native StackMap DexPc, independent int/root witnesses and both slow/fast reads.
 actual_physical=true;
 for(unsigned i=0;i<3;++i) {
  physical_quick[i]={};void* owner=&method_ids[i];
  std::memcpy(physical_quick[i].data(),&owner,sizeof(owner));
  physical_quick[i][2]=quick[2+2*i];physical_quick[i][3]=quick[3+2*i];
 }
 catalog[0]=1;catalog[1]=3;catalog[2]=1;catalog[3]=4;maps[0]=1;maps[1]=2;
 PhysicalOracle physical{{oracle.carrier,oracle.inlinees[0],oracle.inlinees[1]}};
 auto physical_run=[&](){return service.ProvePhysical(&thread,&jit,physical,scratch,2);};
 auto physical_report=physical_run();
 assert(physical_report.failure==Failure::kNone && physical_report.frames==3 &&
   physical_report.physical_owner_mask==7 && physical_report.physical_matches==3 && physical_report.inline_matches==0 &&
   physical_report.inline_flag_checks==0 && physical_report.header_checks==3 &&
   physical_report.dex_pc_checks==3 && physical_report.integers==3 &&
   physical_report.references==3 && physical_report.location_reads==6);
 assert(!std::strcmp(physical_report.detail,"complete-owned-three-physical-proof"));
 // The inline API remains strict: a successful physical proof cannot authorize it.
 assert(run().failure!=Failure::kNone);
 physical_wrong_order=true;assert(physical_run().failure==Failure::kFrameIdentity);physical_wrong_order=false;
 if(f!=Family::kAndroid11){nterp_header=true;assert(physical_run().failure==Failure::kCodeOwnership);nterp_header=false;}
 physical_alias=true;assert(physical_run().failure==Failure::kFrameIdentity);physical_alias=false;
 bad_native=true;assert(physical_run().failure==Failure::kNativeRead);bad_native=false;
 bad_fast=true;assert(physical_run().failure==Failure::kValueMismatch);bad_fast=false;
 bad_pc=true;assert(physical_run().failure==Failure::kDexPc);bad_pc=false;
 bad_owner=true;assert(physical_run().failure==Failure::kCodeOwnership);bad_owner=false;
 ++witnesses[1][0].expected;assert(physical_run().failure==Failure::kValueMismatch);--witnesses[1][0].expected;
 skip_inner=true;assert(physical_run().failure==Failure::kFrameIdentity);skip_inner=false;
 actual_physical=false;

 assert(destroys+1==creates); // only null Context allocation has no destructor.
}
}
int main(){for(auto family:{Family::kAndroid11,Family::kAndroid12,Family::kAndroid13,Family::kAndroid14,Family::kAndroid15,Family::kAndroid16,Family::kAndroid17})Check(family);
 puts("Active frame native proof service: source signatures, original Context lifetime, three-physical and carrier/2-inline independent DexPc/value/root witnesses, fail-closed negative paths passed; host fixtures only");}
