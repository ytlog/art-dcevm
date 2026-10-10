#define WHOLE_FRAME_FIXTURE_ONLY
#include "whole_process_frames_test.cc"
#undef WHOLE_FRAME_FIXTURE_ONLY
namespace {
using LegacyKind=abi::arm64::android11::frame_registers::Kind;
int family=11,location_kind=0;uint32_t location_value=8;
abi::arm64::android11::Method11 legacy_runtime_method{};
bool mark_drift=false,no_mark_helper=false,no_fast=false,no_canonical=false,no_accessible=false,reader_reject=false,debugger_override=false;
uint32_t reader_calls=0;void* read_receiver=nullptr;
constexpr uintptr_t core_bits=0x8877665544332211ULL,fp_bits=0x1122334499aabbccULL;
struct LegacyContext final:Context {
 bool IsAccessibleGPR(uint32_t r)override{return r==2;}
 uintptr_t GetGPR(uint32_t r)override{assert(r==2);return core_bits;}
 bool IsAccessibleFPR(uint32_t r)override{return r==3;}
 uintptr_t GetFPR(uint32_t r)override{assert(r==3);return fp_bits;}
};
abi::arm64::context::Prefix* LegacyCreate(){assert(!exclusive);return new LegacyContext;}
template<class V>void InitLegacy(void* p,void* t,void* c,abi::arm64::stack::WalkKind kind,bool check){
 assert(gc&&exclusive&&list_locked&&check);auto* v=static_cast<V*>(p);
 std::memset(static_cast<unsigned char*>(p)+8,0,sizeof(*v)-8);v->thread=t;v->context=c;v->kind=kind;
}
void LegacyConstruct(void* p,void* t,void* c,abi::arm64::stack::WalkKind kind,bool check){
 if(family==11)InitLegacy<abi::arm64::stack::Visitor11>(p,t,c,kind,check);
 else InitLegacy<abi::arm64::stack::Visitor12>(p,t,c,kind,check);
}
template<class V>void LegacyWalkBody(void* p,bool transitions){
 assert(gc&&exclusive&&list_locked&&transitions);auto* v=static_cast<V*>(p);
 if(v->thread==&empty_thread)return;
 current_method=v->thread==&self?static_cast<void*>(&native_method):&compiled_method;
 quick.method=current_method;v->quick_frame=reinterpret_cast<void**>(&quick);
 if(v->thread==&self){v->VisitFrame();return;}
 if(runtime_frame){
  current_method=family==11?static_cast<void*>(&legacy_runtime_method):&runtime_method;
  std::memcpy(runtime_stack,&current_method,sizeof(current_method));
  const auto saved_pc=reinterpret_cast<uintptr_t>(header)+(family==11?8:4)+32;
  std::memcpy(runtime_stack+runtime_info.size-8,&saved_pc,8);
  v->quick_frame=reinterpret_cast<void**>(runtime_stack);v->header=nullptr;v->pc=0;
  if(!v->VisitFrame())return;
  v->quick_frame=reinterpret_cast<void**>(runtime_stack+runtime_info.size);v->pc=saved_pc;
  current_method=&compiled_method;quick.method=current_method;std::memcpy(v->quick_frame,&quick,sizeof(quick));
 }
 assert(v->thread==&worker);v->header=header;v->pc=reinterpret_cast<uintptr_t>(header)+(family==11?8:4)+32;
 if(inline_frame)v->inline_frames.end.row=1;
 v->VisitFrame();
}
void LegacyWalk(void* p,bool transitions){
 if(family==11)LegacyWalkBody<abi::arm64::stack::Visitor11>(p,transitions);
 else LegacyWalkBody<abi::arm64::stack::Visitor12>(p,transitions);
}
native_runtime_frame::FrameInfo LegacyRuntimeInfo(const void*){assert(current_method==(family==11?static_cast<void*>(&legacy_runtime_method):&runtime_method));return runtime_info;}
template<size_t N>void LegacyTable(abi::arm64::android11::BitTable<N>& table,void* data,size_t bits,uint32_t rows,unsigned width){
 table.region={data,0,bits};table.rows=rows;for(size_t i=0;i<=N;++i)table.columns[i]=i*width;
}
template<class Info>void LegacyInfoBody(Info* storage,const void* h){
 assert(h==header);auto& info=*storage;info={};info.flags=1;info.frame_size=4;info.dex_regs=1;
 LegacyTable(info.stack_maps,stack_row.data(),256,1,32);LegacyTable(info.inline_infos,inline_row.data(),192,1,32);
 LegacyTable(info.dex_masks,&dexmask,2,1,2);LegacyTable(info.dex_maps,dexmaps,64,2,32);
 LegacyTable(info.dex_catalog,catalog,128,2,32);
}
void LegacyInfo11(abi::arm64::android11::CodeInfo* storage,const void* h){LegacyInfoBody(storage,h);}
void LegacyInfo12(abi::arm64::android12::CodeInfo* storage,const void* h){LegacyInfoBody(storage,h);storage->code_size=64;}
abi::arm64::android11::TableIterator LegacyMap11(const abi::arm64::android11::CodeInfo* info,uint32_t pc,int32_t isa){assert(pc==32&&isa==2);return {const_cast<void*>(static_cast<const void*>(&info->stack_maps)),0};}
abi::arm64::android11::TableIterator LegacyMap12(const abi::arm64::android12::CodeInfo* info,uintptr_t pc,int32_t isa){assert(pc==32&&isa==2);return {const_cast<void*>(static_cast<const void*>(&info->stack_maps)),0};}
uint32_t Physical(){
 if(location_kind==0)return quick.values[location_value/4-2];
 if(location_kind==1)return location_value;
 const uintptr_t bits=location_kind<=3?core_bits:fp_bits;
 return (location_kind==3||location_kind==5)?bits>>32:bits;
}
bool LegacyFast(const void* visitor,abi::arm64::android11::frame_registers::Location location,LegacyKind kind,uint32_t* out){
 assert(exclusive&&list_locked&&visitor&&location.kind==location_kind&&location.value==int32_t(location_value));
 assert(kind==abi::arm64::android11::frame_registers::KindForLocation(location_kind));
 if(mark_drift)header[family==11?1:0]&=0x7fffffffu;
 ++reader_calls;read_receiver=const_cast<void*>(visitor);*out=Physical()+(wrong_native_value?1:0);
 if(cursor_drift){if(family==11)++static_cast<abi::arm64::stack::Visitor11*>(read_receiver)->pc;else ++static_cast<abi::arm64::stack::Visitor12*>(read_receiver)->pc;}
 return !reader_reject;
}
bool LegacyCanonical(const void* v,void* method,uint16_t reg,LegacyKind kind,uint32_t* out,
 abi::arm64::android11::frame_registers::OptionalLocation location){
 assert(method==&compiled_method&&reg==0&&location);return LegacyFast(v,*location,kind,out);
}
bool LegacyAccessible(const void* visitor,uint32_t reg,LegacyKind kind,uint32_t* out){
 assert(visitor&&reg==location_value&&kind==abi::arm64::android11::frame_registers::KindForLocation(location_kind));
 *out=Physical();return !reader_reject;
}
bool ModernAccessible(const void* v,uint32_t reg,int32_t kind,uint32_t* out){assert(kind==location_kind);return LegacyAccessible(v,reg,abi::arm64::android11::frame_registers::KindForLocation(kind),out);}
bool LegacyDebugger(const void* v,uint16_t reg,LegacyKind kind,uint32_t*){assert(v&&reg==0&&kind==abi::arm64::android11::frame_registers::KindForLocation(location_kind));return debugger_override;}
bool TypedModernFast(const void* v,abi::arm64::android12::frame_registers::Location loc,uint32_t* out){return LegacyFast(v,loc,abi::arm64::android11::frame_registers::KindForLocation(loc.kind),out);}
void LegacyNativeMap(const void*,uint32_t row,uint32_t first,abi::arm64::dex_register_map::Map* map){assert(row==0&&first==(inline_frame?1u:0u)&&map->count==1);map->data()[0]={location_kind,int32_t(location_value)};}
class LegacyFrameHooks:public FrameHooks {
 public:void* FindSymbol(const char* library,const char* name)override {
 if(no_mark_helper&&!std::strcmp(name,"_ZNK3art20OatQuickMethodHeader23HasShouldDeoptimizeFlagEv"))return nullptr;
#define FN(symbol,fn) if(!std::strcmp(name,symbol))return Fn(fn)
 FN(abi::arm64::context::kCreate,LegacyCreate);FN(abi::arm64::stack::kConstruct,LegacyConstruct);FN(abi::arm64::stack::kWalk,LegacyWalk);
 FN(native_runtime_frame::Service::kFrameInfo,LegacyRuntimeInfo);
 if(!std::strcmp(name,abi::arm64::android11::kConstructCodeInfo))return family==11?Fn(LegacyInfo11):Fn(LegacyInfo12);FN(abi::arm64::android11::kFindStackMap,LegacyMap11);FN(abi::arm64::android12::kFindStackMap,LegacyMap12);
 FN(abi::arm64::dex_register_map::kDecode,LegacyNativeMap);
 if(!std::strcmp(name,abi::arm64::android11::frame_registers::kFast))return no_fast?nullptr:Fn(LegacyFast);
 if(!std::strcmp(name,abi::arm64::android11::frame_registers::kCanonical))return no_canonical?nullptr:Fn(LegacyCanonical);
 if(!std::strcmp(name,abi::arm64::android11::frame_registers::kAccessibleRegister))return no_accessible?nullptr:Fn(LegacyAccessible);
 if(!std::strcmp(name,abi::arm64::android14::frame_registers::kAccessibleRegister))return no_accessible?nullptr:Fn(ModernAccessible);
 FN(abi::arm64::android14::frame_registers::kDebuggerValue,LegacyDebugger);
 if(!std::strcmp(name,abi::arm64::android12::frame_registers::kFast))return no_fast?nullptr:Fn(TypedModernFast);
#undef FN
 return FrameHooks::FindSymbol(library,name);
 }
};
}
int main(){
 LegacyFrameHooks hooks;std::string error;assert(native_source_stop::Install(hooks,&error));
 native_cache::Service cache;assert(cache.Bind(hooks,&thread_list,&error)&&cache.Prepare(&error));
 native_source_stop::Service stop;assert(native_source_stop::Service::Bind(hooks,&heap,State(hooks),&stop,&error));Calibrate(stop);
 for(family=11;family<=12;++family){
  for(int fallback=0;fallback<2;++fallback){
   no_mark_helper=true;header[family==11?1:0]=0x80000000u;
   no_fast=fallback;no_canonical=false;
   whole_process_frames::Service scanner;assert(whole_process_frames::Service::Bind(family==11?code_info_view::SourceFamily::kAndroid11:code_info_view::SourceFamily::kAndroid12,[&](const char* n){return hooks.FindSymbol("libart.so",n);},&scanner));
   no_mark_helper=false;whole_process_frames::Service named;
   assert(whole_process_frames::Service::Bind(family==11?code_info_view::SourceFamily::kAndroid11:code_info_view::SourceFamily::kAndroid12,[&](const char* n){return hooks.FindSymbol("libart.so",n);},&named));no_mark_helper=true;
   whole_process_frames::Service::Prepared storage;assert(scanner.Prepare(&storage));
   auto scan=[&](){self.flags=0;native_source_stop::Session owner(stop);assert(owner.Begin(&self,sizeof(self)));forbid_native_allocation=true;auto report=(no_mark_helper?scanner:named).Scan(owner,&self,cache,&jit_cache,storage);forbid_native_allocation=false;return report;};
   for(location_kind=0;location_kind<=5;++location_kind){
    location_value=location_kind==0?8:location_kind==1?123:location_kind<=3?2:3;
    catalog[0]=catalog[2]=location_kind+1;catalog[1]=catalog[3]=(location_kind==0?location_value/4:location_value)+1;
    for(bool inlined:{false,true}){inline_frame=inlined;reader_calls=0;auto report=scan();assert(report.accepted&&report.compiled_frames==1&&report.native_value_reads==1&&reader_calls==1&&read_receiver);}
    inline_frame=false;reader_reject=true;assert(!scan().accepted);reader_reject=false;
    wrong_native_value=true;assert(!scan().accepted);wrong_native_value=false;
   }
   location_kind=0;location_value=8;catalog[0]=catalog[2]=1;catalog[1]=catalog[3]=3;
   legacy_runtime_method.dex_method_index=runtime_method.dex_method_index=UINT32_MAX;
   runtime_frame=true;auto runtime_report=scan();assert(runtime_report.accepted&&runtime_report.runtime_frames==1&&runtime_report.compiled_frames==1);runtime_frame=false;
   header[family==11?1:0]=0;assert(!scan().accepted);header[family==11?1:0]=0x80000000u;
   for(auto* flag:{&aot,&nterp,&wrong_dexpc,&cursor_drift}){*flag=true;assert(!scan().accepted);*flag=false;}
   mark_drift=true;assert(!scan().accepted);mark_drift=false;header[family==11?1:0]=0x80000000u;
   no_mark_helper=false;unmarked=true;assert(!scan().accepted);unmarked=false;
   if(family==11||fallback){debugger_override=true;assert(!scan().accepted);debugger_override=false;}
  }
 }
 family=11;no_fast=no_canonical=false;no_accessible=true;no_mark_helper=true;
 location_kind=3;location_value=2;catalog[0]=catalog[2]=4;catalog[1]=catalog[3]=3;
 whole_process_frames::Service inaccessible;assert(whole_process_frames::Service::Bind(code_info_view::SourceFamily::kAndroid11,[&](const char* n){return hooks.FindSymbol("libart.so",n);},&inaccessible));
 whole_process_frames::Service::Prepared inaccessible_storage;assert(inaccessible.Prepare(&inaccessible_storage));self.flags=0;
 {native_source_stop::Session owner(stop);assert(owner.Begin(&self,sizeof(self)));assert(!inaccessible.Scan(owner,&self,cache,&jit_cache,inaccessible_storage).accepted);}
 no_accessible=false;location_kind=0;location_value=8;catalog[0]=catalog[2]=1;catalog[1]=catalog[3]=3;
 family=11;no_fast=no_canonical=true;
 whole_process_frames::Service missing;assert(whole_process_frames::Service::Bind(code_info_view::SourceFamily::kAndroid11,[&](const char* n){return hooks.FindSymbol("libart.so",n);},&missing));
 whole_process_frames::Service::Prepared storage;assert(missing.Prepare(&storage));self.flags=0;
 {native_source_stop::Session owner(stop);assert(owner.Begin(&self,sizeof(self)));assert(!missing.Scan(owner,&self,cache,&jit_cache,storage).accepted);}
 assert(!gc&&!exclusive&&!list_locked&&stops==resumes);
 std::cout<<"legacy 11/12 complete physical+inlined stack/GPR/FPR/constant maps, typed canonical fallback, missing readers/mark/ownership rejection PASS\n";
}
