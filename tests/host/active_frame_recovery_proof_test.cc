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
bool native_map_mismatch=false,native_count_mismatch=false,native_depth_mismatch=false;
bool debugger_override=false,bad_native_offset=false,bad_context_value=false,context_swap=false;unsigned canonical_calls=0,native_fast_calls=0;
bool cursor_method_drift=false,cursor_pc_drift=false;unsigned method_queries=0,dex_queries=0,slow_calls=0;
alignas(8) std::array<std::array<uint32_t,16>,3> physical_quick{};
uint32_t code_size=64;unsigned creates=0,resets=0,destroys=0,reference_fast11=0;
struct Context final:abi::arm64::context::Prefix {void Reset()override{++resets;}~Context()override{++destroys;}
 void FillCalleeSaves(uint8_t*,const abi::arm64::context::FrameInfo&)override{}
 void SetSP(uintptr_t)override{}void SetPC(uintptr_t)override{}void SetArg0(uintptr_t)override{}
 bool IsAccessibleGPR(uint32_t reg)override{return !inaccessible&&reg==7;}uintptr_t* GetGPRAddress(uint32_t)override{return nullptr;}
 uintptr_t GetGPR(uint32_t reg)override{assert(reg==7);return (uintptr_t{123}<<32)|123;}void SetGPR(uint32_t,uintptr_t)override{}
 bool IsAccessibleFPR(uint32_t reg)override{return !inaccessible&&reg==7;}uintptr_t GetFPR(uint32_t reg)override{assert(reg==7);return (uintptr_t{123}<<32)|123;}};
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
  method_queries=dex_queries=0;v->depth=i;v->quick_frame=reinterpret_cast<void**>(quick.data());v->header=header;
  v->pc=reinterpret_cast<uintptr_t>(header)+(family==Family::kAndroid11?8:4)+32;
  v->inline_frames.begin.row=0;v->inline_frames.end.row=(bad_inline && i==1)?2:2-i;
  if(physical_inlinees&&i<2)v->inline_frames.end.row=0;
  active_index=physical_wrong_order?i:2-i;active_method=&method_ids[active_index];
  if(actual_physical) {
   v->inline_frames.end.row=0;
   v->quick_frame=reinterpret_cast<void**>(physical_quick[physical_alias?0:active_index].data());
  }
  void* original_context=v->context;if(context_swap)v->context=nullptr;
  const bool keep=v->VisitFrame();v->context=original_context;if(!keep)break;
 }
}
template<class V> void ChangePc(const void* raw){auto* v=const_cast<V*>(static_cast<const V*>(raw));++v->pc;}
void DriftPc(const void* raw) {
 switch(family){case Family::kAndroid11:ChangePc<abi::arm64::stack::Visitor11>(raw);break;
 case Family::kAndroid12:ChangePc<abi::arm64::stack::Visitor12>(raw);break;
 case Family::kAndroid17:ChangePc<abi::arm64::stack::Visitor17>(raw);break;
 default:ChangePc<abi::arm64::stack::Visitor13>(raw);break;}
}
uint32_t DexPc(const void* raw,bool abort){assert(!abort);if(cursor_pc_drift && ++dex_queries>1)DriftPc(raw);for(unsigned i=0;i<3;++i)if(active_method==&method_ids[i])return 10+i+(bad_pc?1:0);return UINT32_MAX;}
void* Method(const void*){return cursor_method_drift && ++method_queries>1?&method_ids[(active_index+1)%3]:active_method;}
template<class V> void CheckCursor(const void* raw,void* method) {
 const auto* v=static_cast<const V*>(raw);assert(v->header==header && v->context);
 assert(v->pc==reinterpret_cast<uintptr_t>(header)+(family==Family::kAndroid11?8:4)+32);
 assert(method==active_method);
 if(actual_physical){assert(v->inline_frames.begin.row==v->inline_frames.end.row);
   assert(v->quick_frame==reinterpret_cast<void**>(physical_quick[active_index].data()));
   assert(*v->quick_frame==method);
 }else{assert(v->quick_frame==reinterpret_cast<void**>(quick.data()));
   assert(v->inline_frames.end.row==active_index);}
}
bool Slow(const void* raw,void* method,uint16_t reg,Kind kind,uint32_t* value) {
 ++slow_calls;
 switch(family){case Family::kAndroid11:CheckCursor<abi::arm64::stack::Visitor11>(raw,method);break;
 case Family::kAndroid12:CheckCursor<abi::arm64::stack::Visitor12>(raw,method);break;
 case Family::kAndroid17:CheckCursor<abi::arm64::stack::Visitor17>(raw,method);break;
 default:CheckCursor<abi::arm64::stack::Visitor13>(raw,method);break;}
 assert((reg==0 && kind==Kind::kInt)||(reg==1 && kind==Kind::kReference));
 if(bad_native)return false;
 for(unsigned i=0;i<3;++i)if(method==&method_ids[i]){*value=quick[2+2*i+reg];return true;}
 return false;
}
bool SlowModern(const void* visitor,void* method,uint16_t reg,Kind kind,uint32_t* value,bool full) {
 assert(!full);return Slow(visitor,method,reg,kind,value);
}
void CheckActiveCursor(const void* raw) {
 switch(family){case Family::kAndroid11:CheckCursor<abi::arm64::stack::Visitor11>(raw,active_method);break;
 case Family::kAndroid12:CheckCursor<abi::arm64::stack::Visitor12>(raw,active_method);break;
 case Family::kAndroid17:CheckCursor<abi::arm64::stack::Visitor17>(raw,active_method);break;
 default:CheckCursor<abi::arm64::stack::Visitor13>(raw,active_method);break;}
}
template<class V> abi::arm64::context::Prefix* NativeContext(const void* raw){return static_cast<abi::arm64::context::Prefix*>(static_cast<const V*>(raw)->context);}
abi::arm64::context::Prefix* CurrentContext(const void* raw) {
 switch(family){case Family::kAndroid11:return NativeContext<abi::arm64::stack::Visitor11>(raw);
 case Family::kAndroid12:return NativeContext<abi::arm64::stack::Visitor12>(raw);
 case Family::kAndroid17:return NativeContext<abi::arm64::stack::Visitor17>(raw);
 default:return NativeContext<abi::arm64::stack::Visitor13>(raw);}
}
size_t NativeOffset(const void* raw){CheckActiveCursor(raw);return 32+(bad_native_offset?4:0);}
bool Accessible(const void* raw,uint32_t reg,int32_t kind,uint32_t* value) {
 auto* c=CurrentContext(raw);assert(c && (kind==2||kind==4));
 if(kind==2?!c->IsAccessibleGPR(reg):!c->IsAccessibleFPR(reg))return false;
 *value=static_cast<uint32_t>(kind==2?c->GetGPR(reg):c->GetFPR(reg))+(bad_context_value?1:0);return true;
}
bool DebuggerValue(const void* raw,uint16_t,Kind kind,uint32_t*) {CheckActiveCursor(raw);assert(kind==Kind::kInt);return debugger_override;}
bool Fast(const void* raw,NativeLocation location,uint32_t* value) {
 ++native_fast_calls;CheckActiveCursor(raw);if(bad_native)return false;
 if(location.kind==0){assert(location.value>=8&&location.value<=28);
   *value=(actual_physical?physical_quick[active_index][location.value/4]:quick[location.value/4]);}
 else if(location.kind==1)*value=location.value;
 else {
   assert(location.kind>=2&&location.kind<=5&&location.value==7);auto* c=CurrentContext(raw);
   const bool core=location.kind==2||location.kind==3;
   if(core?!c->IsAccessibleGPR(7):!c->IsAccessibleFPR(7))return false;
   uintptr_t bits=core?c->GetGPR(7):c->GetFPR(7);
   if(location.kind==3||location.kind==5)bits>>=32;*value=static_cast<uint32_t>(bits);
 }
 *value+=bad_fast?1:0;return true;
}
bool Fast11(const void* visitor,NativeLocation location,Kind kind,uint32_t* value){assert(kind==((location.value%8)==4?Kind::kReference:Kind::kInt));if(kind==Kind::kReference)++reference_fast11;return Fast(visitor,location,value);}
size_t InlineDepth(const void*){return (actual_physical?0:active_index)+(native_depth_mismatch?1:0);}
size_t RegisterCount(const void* raw,void* info,int depth) {
 switch(family){case Family::kAndroid11:CheckCursor<abi::arm64::stack::Visitor11>(raw,active_method);break;
 case Family::kAndroid12:CheckCursor<abi::arm64::stack::Visitor12>(raw,active_method);break;
 case Family::kAndroid17:CheckCursor<abi::arm64::stack::Visitor17>(raw,active_method);break;
 default:CheckCursor<abi::arm64::stack::Visitor13>(raw,active_method);break;}
 code_info_view::View view;assert(code_info_view::ReadSourceObject(family,info,400,&view));
 assert(depth==static_cast<int>(actual_physical?0:active_index));
 if(native_count_mismatch)return 1;
 if(!depth)return view.dex_regs;
 uint32_t count;assert(view.InlineInfos().Read(depth-1,5,&count));return count;
}
void DecodeNativeMap(const void* info,uint32_t row,uint32_t first,abi::arm64::dex_register_map::Map* map) {
 code_info_view::View view;assert(code_info_view::ReadSourceObject(family,info,400,&view));
 std::vector<decoded_frame_recovery::Location> locations(map->count);
 decoded_frame_recovery::RegisterRange range;
 const uint32_t begin=0,end=actual_physical?0:active_index;
 assert(decoded_frame_recovery::Decode(view,row,begin,end,locations.data(),locations.size(),&range));
 assert(range.first==first && range.count==map->count);
 for(size_t i=0;i<map->count;++i)map->data()[i]={locations[i].kind,static_cast<int32_t>(locations[i].value)};
 if(native_map_mismatch)map->data()[map->count-1].kind=-1;
}
bool Canonical(const void* raw,void* method,uint16_t reg,Kind kind,uint32_t* value,
    abi::arm64::android14::frame_registers::OptionalLocation location,bool full) {
 ++canonical_calls;CheckActiveCursor(raw);assert(method==active_method && reg<2 && kind==Kind::kInt && location.has_value() && !full);
 return Fast(raw,*location,value);
}
struct Finder {
 const char* missing=nullptr;bool no_native_reader=false;const char* missing_secondary=nullptr;
 template<class T> static void* Fn(T function){return reinterpret_cast<void*>(function);}
 void* operator()(const char* name)const {
  if((missing && std::strcmp(missing,name)==0)||(missing_secondary && std::strcmp(missing_secondary,name)==0))return nullptr;
  if(no_native_reader && (std::strcmp(name,abi::arm64::android12::frame_registers::kFast)==0||std::strcmp(name,abi::arm64::android14::frame_registers::kCanonical)==0))return nullptr;
  if(std::strcmp(name,abi::arm64::android14::frame_registers::kCanonical)==0)return Fn(Canonical);
  if(std::strcmp(name,abi::arm64::android14::frame_registers::kDebuggerValue)==0)return Fn(DebuggerValue);
  if(std::strcmp(name,abi::arm64::android14::frame_registers::kNativePcOffset)==0)return Fn(NativeOffset);
  if(std::strcmp(name,abi::arm64::android14::frame_registers::kAccessibleRegister)==0)return Fn(Accessible);
  if(wrong_geometry && std::strcmp(name,abi::arm64::stack::kConstruct)==0)
   return family==Family::kAndroid12 || family==Family::kAndroid17?Fn(Construct<abi::arm64::stack::Visitor13>):Fn(Construct<abi::arm64::stack::Visitor12>);
  if(std::strcmp(name,"_ZNK3art12StackVisitor11InlineDepthEv")==0)return Fn(InlineDepth);
  if(std::strcmp(name,"_ZNK3art12StackVisitor20GetNumberOfRegistersEPNS_8CodeInfoEi")==0)return Fn(RegisterCount);
  if(std::strcmp(name,abi::arm64::dex_register_map::kDecode)==0)return Fn(DecodeNativeMap);
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
 actual_physical=physical_alias=physical_wrong_order=nterp_header=cursor_method_drift=cursor_pc_drift=native_map_mismatch=native_count_mismatch=native_depth_mismatch=debugger_override=bad_native_offset=bad_context_value=context_swap=false;slow_calls=canonical_calls=native_fast_calls=0;
 for(unsigned i=0;i<3;++i){quick[2+2*i]=123+111*i;quick[3+2*i]=0x11000+0x1000*i;}
 stack={};stack[2]=11;stack[4]=stack[5]=stack[6]=stack[7]=1;inlines={};inlines[0]=1;inlines[1]=12;inlines[7]=13;inlines[5]=5;inlines[11]=7;
 header[0]=16;header[1]=64;code_size=64;mask=63;roots=0xa8;for(unsigned i=0;i<6;++i){maps[i]=i+1;catalog[2*i]=1;catalog[2*i+1]=i+3;}
}
void Check(Family f){family=f;Reset();Service service;
 assert(!Service::Bind(f,Finder{abi::arm64::context::kCreate},&service));
 assert(!Service::Bind(f,Finder{abi::arm64::dex_register_map::kDecode},&service));

 const bool modern=f==Family::kAndroid14||f==Family::kAndroid15||f==Family::kAndroid16||f==Family::kAndroid17;
 if(modern){assert(Service::Bind(f,Finder{abi::arm64::android14::frame_registers::kSlow},&service));assert(!Service::Bind(f,Finder{nullptr,true},&service));}
 else assert(!Service::Bind(f,Finder{abi::arm64::android11::frame_registers::kSlow},&service));
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
 assert(physical_report.native_location_reads==6 && physical_report.physical_comparison_reads==6 &&
   physical_report.raw_reference_reads==3 && physical_report.native_context_checks==6 &&
   physical_report.native_pc_offset_observed && physical_report.native_pc_offset==32);
 if(modern) {
   Service canonical_reader;
   assert(Service::Bind(f,Finder{abi::arm64::android12::frame_registers::kFast},&canonical_reader));
   slow_calls=canonical_calls=native_fast_calls=0;
   const auto canonical_report=canonical_reader.ProvePhysical(&thread,&jit,physical,scratch,2);
   assert(canonical_report.failure==Failure::kNone && canonical_calls==6 && slow_calls==0 &&
      canonical_report.native_location_reads==6 && canonical_report.physical_comparison_reads==6 &&
      !std::strcmp(canonical_report.reader_route,"original-canonical-optional-location-fast"));
   debugger_override=true;canonical_calls=0;
   assert(canonical_reader.ProvePhysical(&thread,&jit,physical,scratch,2).failure==Failure::kNativeRead && canonical_calls==0);
   debugger_override=false;
   Service no_debugger_guard;
   assert(!Service::Bind(f,Finder{abi::arm64::android12::frame_registers::kFast,false,abi::arm64::android14::frame_registers::kDebuggerValue},&no_debugger_guard));
 }
 bad_native_offset=true;native_fast_calls=slow_calls=0;
 assert(physical_run().failure==Failure::kCodeOwnership && native_fast_calls==0 && slow_calls==0);bad_native_offset=false;
 bad_context_value=true;assert(physical_run().failure==Failure::kFrameIdentity);bad_context_value=false;
 context_swap=true;assert(physical_run().failure==Failure::kFrameIdentity);context_swap=false;
 Service folded_pc;assert(Service::Bind(f,Finder{abi::arm64::android14::frame_registers::kNativePcOffset},&folded_pc));
 const auto folded_pc_report=folded_pc.ProvePhysical(&thread,&jit,physical,scratch,2);
 assert(folded_pc_report.failure==Failure::kNone && !folded_pc_report.native_pc_offset_observed && folded_pc_report.native_location_reads==6);
 // The inline API remains strict: a successful physical proof cannot authorize it.
 assert(run().failure!=Failure::kNone);
 if(f==Family::kAndroid14||f==Family::kAndroid15||f==Family::kAndroid16) {
   Service missing_count;const char* missing_count_name=nullptr;
   const unsigned created_before=creates;slow_calls=0;
   assert(!Service::Bind(f,Finder{"_ZNK3art12StackVisitor20GetNumberOfRegistersEPNS_8CodeInfoEi"},&missing_count,&missing_count_name));
   assert(missing_count_name && !std::strcmp(missing_count_name,"_ZNK3art12StackVisitor20GetNumberOfRegistersEPNS_8CodeInfoEi"));
   assert(creates==created_before && slow_calls==0);
 }
 if(f==Family::kAndroid17) {
   Service folded_count;assert(Service::Bind(f,Finder{"_ZNK3art12StackVisitor20GetNumberOfRegistersEPNS_8CodeInfoEi"},&folded_count));
   const auto folded_report=folded_count.ProvePhysical(&thread,&jit,physical,scratch,2);
   assert(folded_report.failure==Failure::kNone && !folded_report.native_register_count_observed &&
      folded_report.native_register_count==2 && folded_report.slow_register_expected_count==2 &&
      !std::strcmp(folded_report.register_count_source,"fixed17-original-helper-equivalent"));
   ++physical.callers[2].registers;
   assert(folded_count.ProvePhysical(&thread,&jit,physical,scratch,2).failure==Failure::kDecodedMap);
   --physical.callers[2].registers;
 }
 assert(physical_report.native_register_count_observed && physical_report.native_register_count==2 &&
   physical_report.slow_register_expected_count==2 && physical_report.native_inline_depth_observed &&
   physical_report.native_inline_depth==0 && physical_report.source_inline_depth==0);
 slow_calls=0;native_depth_mismatch=true;auto depth_failed=physical_run();
 assert(depth_failed.failure==Failure::kFrameIdentity && depth_failed.native_inline_depth==1 &&
   depth_failed.source_inline_depth==0 && slow_calls==0);native_depth_mismatch=false;
 slow_calls=0;native_count_mismatch=true;assert(physical_run().failure==Failure::kDecodedMap && slow_calls==0);native_count_mismatch=false;
 slow_calls=0;native_map_mismatch=true;assert(physical_run().failure==Failure::kDecodedMap && slow_calls==0);native_map_mismatch=false;
 slow_calls=0;cursor_method_drift=true;assert(physical_run().failure==Failure::kFrameIdentity && slow_calls==0);cursor_method_drift=false;
 slow_calls=0;cursor_pc_drift=true;assert(physical_run().failure==Failure::kFrameIdentity && slow_calls==0);cursor_pc_drift=false;
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
int main(){
 for(size_t count:{0u,1u,16u,17u,64u}) {
   abi::arm64::dex_register_map::Map map(count);assert(map.count==count);
   assert(map.data()==(count<=16?map.small.data():map.large.data()));
   for(size_t i=0;i<count;++i){assert(map.data()[i].kind==-2);map.data()[i]={0,static_cast<int32_t>(i*4)};}
   for(size_t i=0;i<count;++i)assert(map.data()[i].value==static_cast<int32_t>(i*4));
 }
 for(auto family:{Family::kAndroid11,Family::kAndroid12,Family::kAndroid13,Family::kAndroid14,Family::kAndroid15,Family::kAndroid16,Family::kAndroid17})Check(family);
 puts("Active frame native proof service: source signatures, original Context lifetime, three-physical and carrier/2-inline independent DexPc/value/root witnesses, fail-closed negative paths passed; host fixtures only");}
